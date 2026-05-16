#include "LockWorkflow.h"

#include "PoorforceLog.h"
#include "PoorforceConfig.h"
#include "PoorforcePathResolver.h"
#include "LockServerClient.h"
#include "RcloneProcessManager.h"

#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "UObject/Object.h"
#include "UObject/Package.h"

FLockWorkflow::FLockWorkflow(
	const FPoorforceConfig& InConfig,
	TSharedPtr<FLockServerClient> InClient,
	TSharedPtr<FRcloneProcessManager> InRclone,
	FString InUserId)
	: Config(InConfig)
	, Client(MoveTemp(InClient))
	, Rclone(MoveTemp(InRclone))
	, UserId(MoveTemp(InUserId))
{
}

bool FLockWorkflow::Resolve(UObject* Asset, FResolvedAsset& Out) const
{
	if (!IsValid(Asset)) return false;

	UPackage* Package = Asset->GetPackage();
	if (!IsValid(Package)) return false;

	Out.PackageName = Package->GetName();

	Out.Match = PoorforcePathResolver::ResolveLongestPrefix(Out.PackageName, Config.ManagedPaths);
	if (Out.Match == nullptr) return false;

	Out.RelativePath = PoorforcePathResolver::MakeRelativePath(Out.PackageName, *Out.Match);
	Out.LockKey      = PoorforcePathResolver::MakeLockKey(Config.LockKeyNamespace, Out.RelativePath);

	if (Out.Match->Mode == EPoorforcePathMode::LockAndSync)
	{
		const FString Extension = PoorforcePathResolver::GetPackageExtensionFor(Asset);
		PoorforcePathResolver::MakeLocalFilePath(Out.PackageName, Asset, Out.LocalFilePath);
		Out.RemoteFilePath = PoorforcePathResolver::MakeRemoteFilePath(Out.Match->RcloneRemote, Out.RelativePath, Extension);
	}

	return true;
}

void FLockWorkflow::HandleAssetOpened(UObject* Asset)
{
	if (!Client.IsValid()) return;

	FResolvedAsset Resolved;
	if (!Resolve(Asset, Resolved)) return;

	if (InFlightSyncs.Contains(Resolved.LockKey))
	{
		UE_LOG(LogPoorforce, Verbose, TEXT("Skip open handling — sync in flight: %s"), *Resolved.RelativePath);
		return;
	}

	const FString MyUserId = UserId;
	TWeakObjectPtr<UObject> WeakAsset(Asset);

	Client->TryAcquire(Resolved.LockKey, MyUserId, LockTtlSeconds,
		[this, Resolved, MyUserId, WeakAsset](PoorforceLock::EAcquireResult Result)
		{
			switch (Result)
			{
			case PoorforceLock::EAcquireResult::Acquired:
				OwnedLockKeys.Add(Resolved.LockKey);
				UE_LOG(LogPoorforce, Log, TEXT("[Case 1] Lock acquired: %s"), *Resolved.RelativePath);

				if (Resolved.Match->Mode == EPoorforcePathMode::LockAndSync)
				{
					StartDownloadAndReopen(Resolved, WeakAsset);
				}
				return;

			case PoorforceLock::EAcquireResult::NetworkError:
				UE_LOG(LogPoorforce, Warning, TEXT("Lock acquire network error: %s"), *Resolved.RelativePath);
				return;

			case PoorforceLock::EAcquireResult::AlreadyHeld:
				break;
			}

			Client->Get(Resolved.LockKey,
				[this, Resolved, MyUserId](bool bExists, const TOptional<PoorforceLock::FLockEntry>& Entry)
				{
					if (!bExists || !Entry.IsSet())
					{
						UE_LOG(LogPoorforce, Warning, TEXT("Lock vanished between SET and GET: %s"), *Resolved.RelativePath);
						return;
					}

					if (Entry->OwnerId.Equals(MyUserId, ESearchCase::CaseSensitive))
					{
						OwnedLockKeys.Add(Resolved.LockKey);
						UE_LOG(LogPoorforce, Log, TEXT("[Case 2] Lock re-entry, refreshing TTL: %s"), *Resolved.RelativePath);

						Client->Refresh(Resolved.LockKey, LockTtlSeconds,
							[Path = Resolved.RelativePath](bool bSuccess)
							{
								if (!bSuccess)
								{
									UE_LOG(LogPoorforce, Warning, TEXT("Lock TTL refresh failed: %s"), *Path);
								}
							});
						return;
					}

					UE_LOG(LogPoorforce, Warning,
						TEXT("[Case 3] BLOCKED: '%s' is held by '%s' (since %s)"),
						*Resolved.RelativePath, *Entry->OwnerId, *Entry->Timestamp);
				});
		});
}

void FLockWorkflow::HandleAssetClosed(UObject* Asset)
{
	if (!Client.IsValid()) return;

	FResolvedAsset Resolved;
	if (!Resolve(Asset, Resolved)) return;

	if (InFlightSyncs.Contains(Resolved.LockKey))
	{
		UE_LOG(LogPoorforce, Verbose, TEXT("Ignore close — programmatic close during sync: %s"), *Resolved.RelativePath);
		return;
	}

	if (!OwnedLockKeys.Contains(Resolved.LockKey))
	{
		return;
	}

	if (Resolved.Match->Mode == EPoorforcePathMode::LockAndSync)
	{
		StartUploadAndRelease(Resolved);
		return;
	}

	Client->Release(Resolved.LockKey,
		[this, Key = Resolved.LockKey, Path = Resolved.RelativePath](bool bSuccess)
		{
			OwnedLockKeys.Remove(Key);

			if (bSuccess)
			{
				UE_LOG(LogPoorforce, Log, TEXT("Lock released: %s"), *Path);
			}
			else
			{
				UE_LOG(LogPoorforce, Warning, TEXT("Lock release failed (removed from owned set anyway): %s"), *Path);
			}
		});
}

void FLockWorkflow::StartDownloadAndReopen(const FResolvedAsset& Resolved, TWeakObjectPtr<UObject> WeakAsset)
{
	if (!Rclone.IsValid())
	{
		UE_LOG(LogPoorforce, Warning, TEXT("LockAndSync but rclone manager is missing — skipping download: %s"), *Resolved.RelativePath);
		return;
	}

	if (Resolved.LocalFilePath.IsEmpty() || Resolved.RemoteFilePath.IsEmpty())
	{
		UE_LOG(LogPoorforce, Warning, TEXT("LockAndSync path resolution failed — skipping download: %s"), *Resolved.RelativePath);
		return;
	}

	InFlightSyncs.Add(Resolved.LockKey);

	CloseAssetEditor(WeakAsset);

	NotifyUser(FString::Printf(TEXT("Syncing %s from remote..."), *Resolved.RelativePath));

	Rclone->StartCopyTo(
		FRcloneProcessManager::EDirection::Download,
		Resolved.LocalFilePath,
		Resolved.RemoteFilePath,
		[this, Resolved, WeakAsset](bool bSuccess, int32 ExitCode, const FString& Output)
		{
			InFlightSyncs.Remove(Resolved.LockKey);

			if (bSuccess)
			{
				UE_LOG(LogPoorforce, Log, TEXT("Download complete: %s"), *Resolved.RelativePath);
				ReopenAssetEditor(WeakAsset);
				return;
			}

			UE_LOG(LogPoorforce, Warning,
				TEXT("Download failed for %s (exit=%d). Releasing lock so others aren't blocked."),
				*Resolved.RelativePath, ExitCode);

			NotifyUserWarning(FString::Printf(TEXT("Download failed: %s"), *Resolved.RelativePath));

			Client->Release(Resolved.LockKey,
				[this, Key = Resolved.LockKey](bool)
				{
					OwnedLockKeys.Remove(Key);
				});
		});
}

void FLockWorkflow::StartUploadAndRelease(const FResolvedAsset& Resolved)
{
	if (!Rclone.IsValid())
	{
		UE_LOG(LogPoorforce, Warning, TEXT("LockAndSync close but rclone manager missing — releasing lock without upload: %s"), *Resolved.RelativePath);

		Client->Release(Resolved.LockKey,
			[this, Key = Resolved.LockKey](bool) { OwnedLockKeys.Remove(Key); });
		return;
	}

	if (Resolved.LocalFilePath.IsEmpty() || Resolved.RemoteFilePath.IsEmpty())
	{
		UE_LOG(LogPoorforce, Warning, TEXT("LockAndSync close path resolution failed — releasing lock without upload: %s"), *Resolved.RelativePath);

		Client->Release(Resolved.LockKey,
			[this, Key = Resolved.LockKey](bool) { OwnedLockKeys.Remove(Key); });
		return;
	}

	InFlightSyncs.Add(Resolved.LockKey);

	NotifyUser(FString::Printf(TEXT("Uploading %s..."), *Resolved.RelativePath));

	Rclone->StartCopyTo(
		FRcloneProcessManager::EDirection::Upload,
		Resolved.LocalFilePath,
		Resolved.RemoteFilePath,
		[this, Resolved](bool bSuccess, int32 ExitCode, const FString& Output)
		{
			InFlightSyncs.Remove(Resolved.LockKey);

			if (!bSuccess)
			{
				UE_LOG(LogPoorforce, Warning,
					TEXT("Upload failed for %s (exit=%d). Lock kept; manual retry required."),
					*Resolved.RelativePath, ExitCode);
				NotifyUserWarning(FString::Printf(TEXT("Upload failed (lock kept): %s"), *Resolved.RelativePath));
				return;
			}

			UE_LOG(LogPoorforce, Log, TEXT("Upload complete: %s"), *Resolved.RelativePath);

			Client->Release(Resolved.LockKey,
				[this, Key = Resolved.LockKey, Path = Resolved.RelativePath](bool bReleased)
				{
					OwnedLockKeys.Remove(Key);

					if (bReleased)
					{
						UE_LOG(LogPoorforce, Log, TEXT("Lock released after upload: %s"), *Path);
					}
					else
					{
						UE_LOG(LogPoorforce, Warning, TEXT("Lock release after upload failed (removed from owned set anyway): %s"), *Path);
					}
				});
		});
}

void FLockWorkflow::NotifyUser(const FString& Message) const
{
	FNotificationInfo Info(FText::FromString(Message));
	Info.bFireAndForget = true;
	Info.ExpireDuration = 4.0f;
	FSlateNotificationManager::Get().AddNotification(Info);
}

void FLockWorkflow::NotifyUserWarning(const FString& Message) const
{
	FNotificationInfo Info(FText::FromString(Message));
	Info.bFireAndForget = true;
	Info.ExpireDuration = 6.0f;
	TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
	if (Item.IsValid())
	{
		Item->SetCompletionState(SNotificationItem::CS_Fail);
	}
}

void FLockWorkflow::CloseAssetEditor(TWeakObjectPtr<UObject> WeakAsset)
{
	if (GEditor == nullptr) return;

	UObject* Asset = WeakAsset.Get();
	if (!IsValid(Asset)) return;

	UAssetEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!IsValid(Subsystem)) return;

	Subsystem->CloseAllEditorsForAsset(Asset);
}

void FLockWorkflow::ReopenAssetEditor(TWeakObjectPtr<UObject> WeakAsset)
{
	if (GEditor == nullptr) return;

	UObject* Asset = WeakAsset.Get();
	if (!IsValid(Asset)) return;

	UAssetEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!IsValid(Subsystem)) return;

	Subsystem->OpenEditorForAsset(Asset);
}
