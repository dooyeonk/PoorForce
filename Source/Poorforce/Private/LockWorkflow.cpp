#include "LockWorkflow.h"

#include "PoorforceLog.h"
#include "PoorforceConfig.h"
#include "PoorforcePathResolver.h"
#include "LockServerClient.h"

#include "UObject/Object.h"
#include "UObject/Package.h"

FLockWorkflow::FLockWorkflow(const FPoorforceConfig& InConfig, TSharedPtr<FLockServerClient> InClient, FString InUserId)
	: Config(InConfig)
	, Client(MoveTemp(InClient))
	, UserId(MoveTemp(InUserId))
{
}

bool FLockWorkflow::ResolveKeyForPackage(const FString& PackageName, FString& OutKey, FString& OutRelativePath) const
{
	const FPoorforceManagedPath* Match = PoorforcePathResolver::ResolveLongestPrefix(PackageName, Config.ManagedPaths);
	if (Match == nullptr) return false;

	OutRelativePath = PoorforcePathResolver::MakeRelativePath(PackageName, *Match);
	OutKey = PoorforcePathResolver::MakeLockKey(Config.LockKeyNamespace, OutRelativePath);
	return true;
}

void FLockWorkflow::HandleAssetOpened(UObject* Asset)
{
	if (!IsValid(Asset)) return;
	if (!Client.IsValid()) return;

	UPackage* Package = Asset->GetPackage();
	if (!IsValid(Package)) return;

	const FString PackageName = Package->GetName();

	FString Key;
	FString RelativePath;
	if (!ResolveKeyForPackage(PackageName, Key, RelativePath))
	{
		return;
	}

	const FString MyUserId = UserId;

	Client->TryAcquire(Key, MyUserId, LockTtlSeconds,
		[this, Key, RelativePath, MyUserId](PoorforceLock::EAcquireResult Result)
		{
			switch (Result)
			{
			case PoorforceLock::EAcquireResult::Acquired:
				OwnedLockKeys.Add(Key);
				UE_LOG(LogPoorforce, Log, TEXT("[Case 1] Lock acquired: %s"), *RelativePath);
				return;

			case PoorforceLock::EAcquireResult::NetworkError:
				UE_LOG(LogPoorforce, Warning, TEXT("Lock acquire network error: %s"), *RelativePath);
				return;

			case PoorforceLock::EAcquireResult::AlreadyHeld:
				break;
			}

			Client->Get(Key,
				[this, Key, RelativePath, MyUserId](bool bExists, const TOptional<PoorforceLock::FLockEntry>& Entry)
				{
					if (!bExists || !Entry.IsSet())
					{
						UE_LOG(LogPoorforce, Warning, TEXT("Lock vanished between SET and GET: %s"), *RelativePath);
						return;
					}

					if (Entry->OwnerId.Equals(MyUserId, ESearchCase::CaseSensitive))
					{
						OwnedLockKeys.Add(Key);
						UE_LOG(LogPoorforce, Log, TEXT("[Case 2] Lock re-entry, refreshing TTL: %s"), *RelativePath);

						Client->Refresh(Key, LockTtlSeconds,
							[RelativePath](bool bSuccess)
							{
								if (!bSuccess)
								{
									UE_LOG(LogPoorforce, Warning, TEXT("Lock TTL refresh failed: %s"), *RelativePath);
								}
							});
						return;
					}

					UE_LOG(LogPoorforce, Warning,
						TEXT("[Case 3] BLOCKED: '%s' is held by '%s' (since %s)"),
						*RelativePath, *Entry->OwnerId, *Entry->Timestamp);
				});
		});
}

void FLockWorkflow::HandleAssetClosed(UObject* Asset)
{
	if (!IsValid(Asset)) return;
	if (!Client.IsValid()) return;

	UPackage* Package = Asset->GetPackage();
	if (!IsValid(Package)) return;

	const FString PackageName = Package->GetName();

	FString Key;
	FString RelativePath;
	if (!ResolveKeyForPackage(PackageName, Key, RelativePath))
	{
		return;
	}

	if (!OwnedLockKeys.Contains(Key))
	{
		return;
	}

	Client->Release(Key,
		[this, Key, RelativePath](bool bSuccess)
		{
			OwnedLockKeys.Remove(Key);

			if (bSuccess)
			{
				UE_LOG(LogPoorforce, Log, TEXT("Lock released: %s"), *RelativePath);
			}
			else
			{
				UE_LOG(LogPoorforce, Warning, TEXT("Lock release failed (removed from owned set anyway): %s"), *RelativePath);
			}
		});
}
