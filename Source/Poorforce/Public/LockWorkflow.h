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
struct FScopedSlowTask;
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

	~FLockWorkflow();

	void HandleAssetOpened(UObject* Asset, bool bSkipInitialDownload = false);
	void HandleAssetEditorOpened(UObject* Asset);
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
	TSet<FString> OpeningAssetPackageNames;

	int32 GetTtlForMode(EPoorforcePathMode Mode) const;

	bool Resolve(UObject* Asset, FResolvedAsset& Out) const;

	void SyncPrefetch(UObject* Asset, const FResolvedAsset& Resolved);
	void AcquireAsyncNoDownload(UObject* Asset, const FResolvedAsset& Resolved);

	void StartUploadAndRelease(const FResolvedAsset& Resolved);

	void CopyNextSidecar(
		FResolvedAsset Resolved,
		int32 Direction,
		int32 Index,
		TFunction<void()> OnAllDone);

	bool DownloadSync(const FString& LocalPath, const FString& RemotePath);

	void HandleBlockedByOther(
		const FResolvedAsset& Resolved,
		const PoorforceLock::FLockEntry& Entry,
		TWeakObjectPtr<UObject> WeakAsset);

	void NotifyUser(const FString& Message) const;
	void NotifyUserWarning(const FString& Message) const;

	static void CloseAssetEditor(TWeakObjectPtr<UObject> WeakAsset);
};
