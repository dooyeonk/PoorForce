#include "PoorforceTimeFormat.h"

#include "Misc/DateTime.h"

namespace PoorforceTimeFormat
{
	FString FormatElapsedSince(int64 UnixSeconds)
	{
		const int64 NowUnix = FDateTime::UtcNow().ToUnixTimestamp();
		const int64 Elapsed = FMath::Max<int64>(0, NowUnix - UnixSeconds);

		if (Elapsed < 60)
		{
			return FString::Printf(TEXT("%lld초 전"), Elapsed);
		}

		if (Elapsed < 3600)
		{
			return FString::Printf(TEXT("%lld분 전"), Elapsed / 60);
		}

		if (Elapsed < 86400)
		{
			const int64 Hours = Elapsed / 3600;
			const int64 Minutes = (Elapsed % 3600) / 60;
			if (Minutes == 0)
			{
				return FString::Printf(TEXT("%lld시간 전"), Hours);
			}
			return FString::Printf(TEXT("%lld시간 %lld분 전"), Hours, Minutes);
		}

		const int64 Days = Elapsed / 86400;
		return FString::Printf(TEXT("%lld일 전"), Days);
	}

	FString FormatElapsedSinceTimestampString(const FString& UnixSecondsString)
	{
		if (UnixSecondsString.IsEmpty()) return FString(TEXT("(시각 정보 없음)"));

		int64 Parsed = 0;
		LexFromString(Parsed, *UnixSecondsString);
		if (Parsed <= 0) return FString(TEXT("(시각 정보 없음)"));

		return FormatElapsedSince(Parsed);
	}
}
