#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

#include "PoorforceConfig.h"

class FLockServerClient;

class FPoorforceModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FPoorforceModule& Get();

	bool IsEnabled() const { return bEnabled; }
	const FPoorforceConfig& GetConfig() const { return Config; }
	TSharedPtr<FLockServerClient> GetLockClient() const { return LockClient; }

private:
	bool bEnabled = false;
	FPoorforceConfig Config;
	TSharedPtr<FLockServerClient> LockClient;
};
