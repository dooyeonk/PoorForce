#pragma once

#include "CoreMinimal.h"

struct FPoorforceManagedPath;

namespace PoorforcePathResolver
{
	const FPoorforceManagedPath* ResolveLongestPrefix(
		const FString& PackageName,
		const TArray<FPoorforceManagedPath>& Paths);

	FString MakeRelativePath(const FString& PackageName, const FPoorforceManagedPath& Match);

	FString MakeLockKey(const FString& Namespace, const FString& RelativePath);
}
