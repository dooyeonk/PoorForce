#include "Poorforce.h"

#include "PoorforceLog.h"
#include "LockServerClient.h"
#include "RcloneProcessManager.h"
#include "DetachedWatcherSpawner.h"
#include "LockWorkflow.h"
#include "AssetEditorInterceptor.h"
#include "UserIdProvider.h"
#include "UI/PoorforceDialogs.h"

#include "Editor.h"

DEFINE_LOG_CATEGORY(LogPoorforce);

#define LOCTEXT_NAMESPACE "FPoorforceModule"

void FPoorforceModule::StartupModule()
{
	UE_LOG(LogPoorforce, Log, TEXT("Poorforce starting up..."));

	if (!PoorforceConfigLoader::LoadFromProjectRoot(Config))
	{
		UE_LOG(LogPoorforce, Warning, TEXT("Poorforce disabled — config not loaded. Expected at: %s"),
			*PoorforceConfigLoader::GetExpectedConfigPath());
		bEnabled = false;
		return;
	}

	const FString& UserId = PoorforceUserId::Get();

	LockClient = MakeShared<FLockServerClient>(Config.UpstashUrl, Config.UpstashToken);
	Rclone = MakeShared<FRcloneProcessManager>(Config.RcloneExecutable);
	Watcher = MakeShared<FDetachedWatcherSpawner>(Config.RcloneExecutable, Config.UpstashUrl, Config.UpstashToken, UserId);
	Watcher->CleanupStaleArtifacts();
	Workflow = MakeUnique<FLockWorkflow>(Config, LockClient, Rclone, Watcher, UserId);

	Interceptor = MakeUnique<FAssetEditorInterceptor>();
	Interceptor->Register(Workflow.Get());

	PreExitHandle = FEditorDelegates::OnEditorPreExit.AddLambda(
		[this]()
		{
			if (Rclone.IsValid() && Rclone->HasActive())
			{
				PoorforceDialogs::WaitForUploadsModal(Rclone);
			}
		});

	bEnabled = true;

	UE_LOG(LogPoorforce, Log, TEXT("Poorforce ready. UserId='%s', ManagedPaths=%d, Rclone='%s'"),
		*UserId, Config.ManagedPaths.Num(), *Config.RcloneExecutable);
}

void FPoorforceModule::ShutdownModule()
{
	if (PreExitHandle.IsValid())
	{
		FEditorDelegates::OnEditorPreExit.Remove(PreExitHandle);
		PreExitHandle.Reset();
	}

	if (Interceptor.IsValid())
	{
		Interceptor->Unregister();
		Interceptor.Reset();
	}

	Workflow.Reset();

	if (Rclone.IsValid())
	{
		Rclone->CancelAll();
		Rclone.Reset();
	}

	Watcher.Reset();
	LockClient.Reset();
	bEnabled = false;

	UE_LOG(LogPoorforce, Log, TEXT("Poorforce shut down."));
}

FPoorforceModule& FPoorforceModule::Get()
{
	return FModuleManager::LoadModuleChecked<FPoorforceModule>(TEXT("Poorforce"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPoorforceModule, Poorforce)
