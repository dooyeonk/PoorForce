#include "PoorforcePathResolver.h"

#include "PoorforceConfig.h"

namespace PoorforcePathResolver
{
	const FPoorforceManagedPath* ResolveLongestPrefix(
		const FString& PackageName,
		const TArray<FPoorforceManagedPath>& Paths)
	{
		const FPoorforceManagedPath* LongestMatch = nullptr;
		int32 LongestLen = 0;

		for (const FPoorforceManagedPath& Candidate : Paths)
		{
			if (Candidate.ContentPath.IsEmpty()) continue;
			if (!PackageName.StartsWith(Candidate.ContentPath, ESearchCase::CaseSensitive)) continue;

			const int32 Len = Candidate.ContentPath.Len();
			if (Len > LongestLen)
			{
				LongestMatch = &Candidate;
				LongestLen = Len;
			}
		}

		return LongestMatch;
	}

	FString MakeRelativePath(const FString& PackageName, const FPoorforceManagedPath& Match)
	{
		if (!PackageName.StartsWith(Match.ContentPath, ESearchCase::CaseSensitive))
		{
			return FString{};
		}

		return PackageName.RightChop(Match.ContentPath.Len());
	}

	FString MakeLockKey(const FString& Namespace, const FString& RelativePath)
	{
		if (Namespace.IsEmpty())
		{
			return FString::Printf(TEXT("asset:%s"), *RelativePath);
		}

		return FString::Printf(TEXT("%s:asset:%s"), *Namespace, *RelativePath);
	}
}
