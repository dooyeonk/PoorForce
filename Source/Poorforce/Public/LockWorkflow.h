#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

namespace PoorforceLock
{
	enum class EAcquireResult : uint8;
	struct FLockEntry;
}

class FLockServerClient;
class FRcloneProcessManager;
struct FPoorforceConfig;
struct FPoorforceManagedPath;
class UObject;

class FLockWorkflow
{
public:
	FLockWorkflow(
		const FPoorforceConfig& InConfig,
		TSharedPtr<FLockServerClient> InClient,
		TSharedPtr<FRcloneProcessManager> InRclone,
		FString InUserId);

	void HandleAssetOpened(UObject* Asset);
	void HandleAssetClosed(UObject* Asset);

private:
	struct FResolvedAsset
	{
		FString PackageName;
		FString RelativePath;
		FString LockKey;
		FString LocalFilePath;
		FString RemoteFilePath;
		const FPoorforceManagedPath* Match = nullptr;
	};

	const FPoorforceConfig& Config;
	TSharedPtr<FLockServerClient> Client;
	TSharedPtr<FRcloneProcessManager> Rclone;
	FString UserId;

	TSet<FString> OwnedLockKeys;
	TSet<FString> InFlightSyncs;

	static constexpr int32 LockTtlSeconds = 259200;

	bool Resolve(UObject* Asset, FResolvedAsset& Out) const;

	void StartDownloadAndReopen(const FResolvedAsset& Resolved, TWeakObjectPtr<UObject> WeakAsset);
	void StartUploadAndRelease(const FResolvedAsset& Resolved);

	void HandleBlockedByOther(
		const FResolvedAsset& Resolved,
		const PoorforceLock::FLockEntry& Entry,
		TWeakObjectPtr<UObject> WeakAsset);

	void NotifyUser(const FString& Message) const;
	void NotifyUserWarning(const FString& Message) const;

	static void ReopenAssetEditor(TWeakObjectPtr<UObject> WeakAsset);
	static void CloseAssetEditor(TWeakObjectPtr<UObject> WeakAsset);
};
