#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

namespace PoorforceLock
{
	enum class EAcquireResult : uint8;
	struct FLockEntry;
}

enum class EPoorforcePathMode : uint8;
class FLockServerClient;
class FRcloneProcessManager;
class FDetachedWatcherSpawner;
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
		TSharedPtr<FDetachedWatcherSpawner> InWatcher,
		FString InUserId);

	void HandleAssetOpened(UObject* Asset, bool bSkipInitialDownload = false);
	void HandleAssetClosed(UObject* Asset);
	void HandleAssetRenamed(const FString& OldPackageName, UObject* NewAsset);

	TArray<FString> GetOwnedLockKeys() const;
	void ManualReleaseLock(const FString& LockKey);

private:
	struct FResolvedAsset
	{
		FString PackageName;
		FString RelativePath;
		FString LockKey;
		FString LocalFilePath;
		FString RemoteFilePath;
		TArray<TPair<FString, FString>> SidecarPaths;
		const FPoorforceManagedPath* Match = nullptr;
	};

	const FPoorforceConfig& Config;
	TSharedPtr<FLockServerClient> Client;
	TSharedPtr<FRcloneProcessManager> Rclone;
	TSharedPtr<FDetachedWatcherSpawner> Watcher;
	FString UserId;

	TSet<FString> OwnedLockKeys;
	TSet<FString> InFlightSyncs;

	int32 GetTtlForMode(EPoorforcePathMode Mode) const;

	bool Resolve(UObject* Asset, FResolvedAsset& Out) const;

	void StartDownloadAndReopen(const FResolvedAsset& Resolved, TWeakObjectPtr<UObject> WeakAsset);
	void StartUploadAndRelease(const FResolvedAsset& Resolved);

	void CopyNextSidecar(
		FResolvedAsset Resolved,
		int32 Direction,
		int32 Index,
		TFunction<void()> OnAllDone);

	void HandleBlockedByOther(
		const FResolvedAsset& Resolved,
		const PoorforceLock::FLockEntry& Entry,
		TWeakObjectPtr<UObject> WeakAsset);

	void NotifyUser(const FString& Message) const;
	void NotifyUserWarning(const FString& Message) const;

	static void ReopenAssetEditor(TWeakObjectPtr<UObject> WeakAsset);
	static void CloseAssetEditor(TWeakObjectPtr<UObject> WeakAsset);
};
