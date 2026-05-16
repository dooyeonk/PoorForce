#pragma once

#include "CoreMinimal.h"

class FLockServerClient;
struct FPoorforceConfig;
class UObject;

class FLockWorkflow
{
public:
	FLockWorkflow(const FPoorforceConfig& InConfig, TSharedPtr<FLockServerClient> InClient, FString InUserId);

	void HandleAssetOpened(UObject* Asset);
	void HandleAssetClosed(UObject* Asset);

private:
	const FPoorforceConfig& Config;
	TSharedPtr<FLockServerClient> Client;
	FString UserId;

	TSet<FString> OwnedLockKeys;

	static constexpr int32 LockTtlSeconds = 259200;

	bool ResolveKeyForPackage(const FString& PackageName, FString& OutKey, FString& OutRelativePath) const;
};
