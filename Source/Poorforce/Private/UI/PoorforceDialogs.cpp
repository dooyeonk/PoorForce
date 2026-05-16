#include "UI/PoorforceDialogs.h"

#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"

namespace
{
	TSharedRef<SWindow> MakeHostWindow(const FText& Title, FVector2D Size)
	{
		return SNew(SWindow)
			.Title(Title)
			.ClientSize(Size)
			.SizingRule(ESizingRule::FixedSize)
			.SupportsMaximize(false)
			.SupportsMinimize(false)
			.HasCloseButton(true);
	}

	TSharedPtr<SWindow> GetParentWindowForModal()
	{
		if (FSlateApplication::IsInitialized())
		{
			TSharedPtr<SWindow> ActiveTop = FSlateApplication::Get().GetActiveTopLevelWindow();
			if (ActiveTop.IsValid()) return ActiveTop;
		}
		return TSharedPtr<SWindow>{};
	}
}

namespace PoorforceDialogs
{
	EBlockedDialogResult ShowBlockedDialog(
		const FString& RelativePath,
		const FString& OwnerId,
		const FString& ElapsedText)
	{
		EBlockedDialogResult Result = EBlockedDialogResult::Confirmed;

		const TSharedRef<SWindow> Host = MakeHostWindow(
			FText::FromString(TEXT("Poorforce — 잠긴 에셋")),
			FVector2D(480.f, 240.f));

		Host->SetContent(
			SNew(SBlockedDialog)
			.RelativePath(RelativePath)
			.OwnerId(OwnerId)
			.ElapsedText(ElapsedText)
			.ParentWindow(Host)
			.OutResult(&Result));

		FSlateApplication::Get().AddModalWindow(Host, GetParentWindowForModal(), /*bSlowTaskWindow=*/ false);

		return Result;
	}

	FForceUnlockDialogResult ShowForceUnlockDialog(
		const FString& RelativePath,
		const FString& OwnerId)
	{
		FForceUnlockDialogResult Result;

		const TSharedRef<SWindow> Host = MakeHostWindow(
			FText::FromString(TEXT("Poorforce — 강제 해제")),
			FVector2D(480.f, 300.f));

		Host->SetContent(
			SNew(SForceUnlockDialog)
			.RelativePath(RelativePath)
			.OwnerId(OwnerId)
			.ParentWindow(Host)
			.OutResult(&Result));

		FSlateApplication::Get().AddModalWindow(Host, GetParentWindowForModal(), /*bSlowTaskWindow=*/ false);

		return Result;
	}
}
