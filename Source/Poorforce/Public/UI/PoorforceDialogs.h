#pragma once

#include "CoreMinimal.h"
#include "UI/SBlockedDialog.h"
#include "UI/SForceUnlockDialog.h"

namespace PoorforceDialogs
{
	EBlockedDialogResult ShowBlockedDialog(
		const FString& RelativePath,
		const FString& OwnerId,
		const FString& ElapsedText);

	FForceUnlockDialogResult ShowForceUnlockDialog(
		const FString& RelativePath,
		const FString& OwnerId);
}
