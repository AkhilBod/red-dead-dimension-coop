#include "FGHud.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "FGBandit.h"
#include "FGIronHorseGameMode.h"
#include "GameFramework/PlayerController.h"
#include "FGTrackerInput.h"
#include "FGTrainPlayer.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    const FLinearColor Cream(0.93f, 0.85f, 0.68f);
    const FLinearColor Ink(0.10f, 0.06f, 0.03f);
    const FLinearColor Brass(0.95f, 0.68f, 0.22f);
    const FLinearColor Blood(0.75f, 0.08f, 0.05f);
}

AFGHud::AFGHud()
{
    static ConstructorHelpers::FObjectFinder<UFont> Roboto(TEXT("/Engine/EngineFonts/Roboto"));
    Font = Roboto.Object;
}

void AFGHud::Box(float X, float Y, float W, float H, FLinearColor Color)
{
    FCanvasTileItem Tile(FVector2D(X, Y), FVector2D(W, H), Color);
    Tile.BlendMode = SE_BLEND_Translucent;
    Canvas->DrawItem(Tile);
}

void AFGHud::Text(const FString& Str, float X, float Y, int32 Size, FLinearColor Color, bool bCentre, bool bShadow)
{
    if (Str.IsEmpty() || !Font) { return; }
    FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(Str), FSlateFontInfo(Font, FMath::Max(8, int32(Size * U)), TEXT("Bold")), Color);
    Item.bCentreX = bCentre;
    Item.bCentreY = bCentre;
    if (bShadow) { Item.EnableShadow(FLinearColor(0, 0, 0, 0.85f * Color.A), FVector2D(2 * U, 2 * U)); }
    Item.BlendMode = SE_BLEND_Translucent;
    Canvas->DrawItem(Item);
}

void AFGHud::Ring(FVector2D C, float Radius, float Thickness, FLinearColor Color, int32 Segments)
{
    FVector2D Prev = C + FVector2D(Radius, 0);
    for (int32 i = 1; i <= Segments; ++i)
    {
        const float A = 2.0f * PI * i / Segments;
        const FVector2D P = C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius;
        FCanvasLineItem Line(Prev, P);
        Line.SetColor(Color);
        Line.LineThickness = Thickness;
        Line.BlendMode = SE_BLEND_Translucent;
        Canvas->DrawItem(Line);
        Prev = P;
    }
}

void AFGHud::Hat(float X, float Y, float S, FLinearColor Color)
{
    Box(X, Y + S * 0.62f, S * 1.5f, S * 0.16f, Color);                 // brim
    Box(X + S * 0.36f, Y + S * 0.12f, S * 0.78f, S * 0.5f, Color);     // crown
    Box(X + S * 0.36f, Y + S * 0.46f, S * 0.78f, S * 0.1f, FLinearColor(Ink.R, Ink.G, Ink.B, Color.A));   // band
}

void AFGHud::DrawHUD()
{
    Super::DrawHUD();
    const AFGIronHorseGameMode* GM = GetWorld() ? Cast<AFGIronHorseGameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
    const AFGTrainPlayer* Player = GM ? GM->Player.Get() : nullptr;
    if (!GM || !Player || !Canvas) { return; }
    const float W = Canvas->SizeX, H = Canvas->SizeY;
    U = H / 1080.0f;
    const UFGTrackerInput* Tracker = Player->Tracker;
    const float Now = GetWorld()->GetRealTimeSeconds();

    // Letterbox, and red when hit
    Box(0, 0, W, H * 0.045f, FLinearColor::Black);
    Box(0, H * 0.955f, W, H * 0.045f, FLinearColor::Black);
    if (Player->HitFlash > 0.0f)
    {
        const float A = Player->HitFlash * 0.45f;
        Box(0, 0, W, H, FLinearColor(Blood.R, Blood.G, Blood.B, A * 0.5f));
        Box(0, 0, W * 0.12f, H, FLinearColor(Blood.R, 0, 0, A));
        Box(W * 0.88f, 0, W * 0.12f, H, FLinearColor(Blood.R, 0, 0, A));
    }
    if (Player->Hats == 1 && !GM->bPlayerDead)
    {
        const float Pulse = 0.12f + 0.08f * FMath::Sin(Now * 5.0f);
        Box(0, 0, W * 0.06f, H, FLinearColor(Blood.R, 0, 0, Pulse));
        Box(W * 0.94f, 0, W * 0.06f, H, FLinearColor(Blood.R, 0, 0, Pulse));
    }

    // ---- result poster
    if (GM->Phase == EFGPhase::Result)
    {
        const float PW = 620 * U, PH = 800 * U, PX = (W - PW) * 0.5f, PY = (H - PH) * 0.5f;
        const float In = FMath::Clamp(GM->ResultTime / 0.6f, 0.0f, 1.0f);
        Box(0, 0, W, H, FLinearColor(0, 0, 0, 0.45f * In));
        Box(PX - 8 * U, PY - 8 * U + (1 - In) * H, PW + 16 * U, PH + 16 * U, Ink);
        Box(PX, PY + (1 - In) * H, PW, PH, Cream);
        if (In >= 1.0f)
        {
            const float CX = W * 0.5f;
            Text(TEXT("END OF THE LINE"), CX, PY + 70 * U, 40, Ink, true, false);
            Box(PX + 40 * U, PY + 115 * U, PW - 80 * U, 4 * U, Ink);
            Text(GM->Rank(), CX, PY + 185 * U, 58, Blood, true, false);
            Text(FString::Printf(TEXT("$%d REWARD"), GM->Score), CX, PY + 265 * U, 34, Ink, true, false);
            float Y = PY + 325 * U;
            auto Row = [&](const FString& L, const FString& R)
            {
                Text(L, PX + 70 * U, Y, 24, Ink, false, false);
                Text(R, PX + PW - 230 * U, Y, 24, Ink, false, false);
                Y += 42 * U;
            };
            Row(TEXT("DISTANCE"), FString::Printf(TEXT("%.1f km"), GM->DistanceM / 1000.0));
            Row(TEXT("BOSSES"), FString::FromInt(GM->BossesBeaten));
            Row(TEXT("BANDITS"), FString::FromInt(GM->Kills));
            Row(TEXT("HEADSHOTS"), FString::FromInt(GM->Headshots));
            Row(TEXT("ACCURACY"), FString::Printf(TEXT("%d%%"), int32(GM->Accuracy() * 100.0f)));
            Row(TEXT("DODGES"), FString::FromInt(GM->Dodges));
            Row(TEXT("BEST DRAW"), GM->DrawTimeMs >= 0.0f ? FString::Printf(TEXT("%d ms"), int32(GM->DrawTimeMs)) : TEXT("--"));
            const FBox2D B = AFGIronHorseGameMode::RideAgainButton();
            const float Pulse = 0.85f + 0.15f * FMath::Sin(Now * 4.0f);
            Box(B.Min.X * W, B.Min.Y * H, (B.Max.X - B.Min.X) * W, (B.Max.Y - B.Min.Y) * H, FLinearColor(Blood.R * Pulse, Blood.G, Blood.B, 1.0f));
            // Cream on the cream poster is invisible, so the label has to fit inside the red button: keep it short.
            Text(TEXT("SHOOT TO RIDE AGAIN"), W * 0.5f, (B.Min.Y + B.Max.Y) * 0.5f * H, 24, Cream, true, false);
            Text(TEXT("any shot, anywhere  (or slap, or Space)"), W * 0.5f, B.Max.Y * H + 26 * U, 16, Cream, true, true);
        }
    }
    else
    {
        // ---- prompts
        if (GM->bStayDown)
        {
            Text(TEXT("STAY DOWN!"), W * 0.5f, H * 0.20f, 80, Brass);
        }
        else if (GM->LeanWarning != 0.0f)
        {
            const float Blink = FMath::Fmod(Now * 3.0f, 1.0f) < 0.6f ? 1.0f : 0.3f;
            const bool bRight = GM->LeanWarning > 0.0f;
            Text(bRight ? TEXT("LEAN RIGHT  >>>") : TEXT("<<<  LEAN LEFT"), W * (bRight ? 0.66f : 0.34f), H * 0.30f, 84, FLinearColor(Brass.R, Brass.G, Brass.B, Blink));
        }
        else if (GM->bDuckWarning)
        {
            const float Blink = FMath::Fmod(Now * 3.0f, 1.0f) < 0.6f ? 1.0f : 0.3f;
            Text(TEXT("DUCK!"), W * 0.5f, H * 0.30f, 110, FLinearColor(Brass.R, Brass.G, Brass.B, Blink));
            Text(TEXT("v  v  v"), W * 0.5f, H * 0.41f, 50, FLinearColor(Brass.R, Brass.G, Brass.B, Blink));
        }
        else if (!GM->Prompt.IsEmpty())
        {
            const bool bBig = GM->Prompt == TEXT("DRAW!");
            Text(GM->Prompt, W * 0.5f, H * (GM->Phase == EFGPhase::Title ? 0.40f : 0.17f), bBig ? 150 : 64, bBig ? Blood : Cream);
        }
        if (!GM->SubPrompt.IsEmpty())
        {
            Text(GM->SubPrompt, W * 0.5f, H * (GM->Phase == EFGPhase::Title ? 0.50f : 0.245f), 30, Brass);
        }
        if (GM->Phase == EFGPhase::Title)
        {
            Text(TEXT("RED DEAD DIMENSION"), W * 0.5f, H * 0.22f, 96, Brass);
        }

        // ---- hats (health), cylinder (ammo), score
        for (int32 i = 0; i < 3; ++i)
        {
            Hat(W - (3 - i) * 95 * U - 30 * U, H * 0.06f + 8 * U, 56 * U, i < Player->Hats ? Cream : FLinearColor(0, 0, 0, 0.45f));
        }
        Text(FString::Printf(TEXT("$%d"), GM->Score), 40 * U, H * 0.06f + 10 * U, 40, Brass, false);
        if (GM->Phase == EFGPhase::Ride || GM->Phase == EFGPhase::Showdown)
        {
            Text(FString::Printf(TEXT("%.1f km    %d mph    lap %d"), GM->DistanceM / 1000.0, int32(GM->TrainSpeed * 2.237f), GM->Lap + 1), 40 * U, H * 0.06f + 62 * U, 18, Cream, false);
        }
        if (GM->BannerTime > 0.0f)
        {
            Text(GM->Banner, W * 0.5f, H * 0.33f, 72, FLinearColor(Brass.R, Brass.G, Brass.B, FMath::Min(1.0f, GM->BannerTime)));
        }

        auto Cylinder = [&](FVector2D Cyl, int32 Rounds)
        {
            Ring(Cyl, 78 * U, 5 * U, Cream, 36);
            for (int32 i = 0; i < Player->MagazineSize; ++i)
            {
                const float A = -PI / 2 + 2 * PI * i / Player->MagazineSize;
                const FVector2D P = Cyl + FVector2D(FMath::Cos(A), FMath::Sin(A)) * 46 * U;
                const float S = 30 * U;
                Box(P.X - S / 2, P.Y - S / 2, S, S, i < Rounds ? Brass : FLinearColor(0, 0, 0, 0.55f));
            }
        };
        Cylinder(FVector2D(W - 120 * U, H * 0.955f - 110 * U), Player->GetCurrentAmmo());
        if (Player->IsDual())
        {
            Cylinder(FVector2D(W - 300 * U, H * 0.955f - 110 * U), Player->AmmoL);
        }

        // ---- incoming shot: a red marker on the barrel that is about to fire, closing in as the 0.7 s run out
        if (APlayerController* PC = GetOwningPlayerController())
        {
            for (const AFGBandit* B : GM->AllBandits())
            {
                const float Warn = B ? B->Warning() : 0.0f;
                FVector2D At;
                if (Warn <= 0.0f || !PC->ProjectWorldLocationToScreen(B->MuzzleLocation(), At)) { continue; }
                const bool bOn = FMath::Fmod(Now * (5.0f + 9.0f * Warn), 1.0f) < 0.55f;
                const FLinearColor Red(1.0f, 0.05f, 0.03f, bOn ? 1.0f : 0.35f);
                Ring(At, (70.0f - 48.0f * Warn) * U, 5 * U, Red, 24);
                Box(At.X - 7 * U, At.Y - 7 * U, 14 * U, 14 * U, Red);
            }
        }

        // ---- crosshair
        const bool bShow = GM->bCrosshairVisible && Tracker->State.bAimValid && !GM->bPlayerDead;
        if (bShow)
        {
            const FVector2D C(GM->AssistedAim.X * W, GM->AssistedAim.Y * H);
            const FLinearColor Col = GM->HitMarker > 0.0f ? Blood : Cream;
            Ring(C, (38 + GM->HitMarker * 14) * U, 5 * U, Col, 36);
            Ring(C, (39 + GM->HitMarker * 14) * U + 3 * U, 2 * U, FLinearColor(0, 0, 0, 0.6f), 36);
            Box(C.X - 5 * U, C.Y - 5 * U, 10 * U, 10 * U, Col);
        }
        if (bShow && Player->IsDual() && Tracker->bTrackerLive)
        {
            // second gun: same ring in brass, so the two can be told apart
            const FVector2D C2(GM->AssistedAim2.X * W, GM->AssistedAim2.Y * H);
            Ring(C2, 38 * U, 5 * U, Brass, 36);
            Ring(C2, 42 * U, 2 * U, FLinearColor(0, 0, 0, 0.6f), 36);
            Box(C2.X - 5 * U, C2.Y - 5 * U, 10 * U, 10 * U, Brass);
        }
        else if (GM->HitMarker > 0.0f && !Tracker->bTrackerLive)
        {
            const FVector2D C(Tracker->State.AimX * W, Tracker->State.AimY * H);
            Ring(C, (38 + GM->HitMarker * 14) * U, 5 * U, Blood, 36);
        }
    }

    // ---- camera preview, bottom left: what the tracker sees
    const float CW = 336 * U, CH = 189 * U, CX = 28 * U, CY = H * 0.955f - CH - 24 * U;
    Box(CX - 5 * U, CY - 5 * U, CW + 10 * U, CH + 38 * U, FLinearColor(Ink.R, Ink.G, Ink.B, 0.85f));
    if (Tracker->HasCameraPreview())
    {
        FCanvasTileItem Tile(FVector2D(CX, CY), Tracker->CameraTexture->GetResource(), FVector2D(CW, CH), FLinearColor::White);
        Tile.BlendMode = SE_BLEND_Opaque;
        Canvas->DrawItem(Tile);
    }
    else
    {
        Box(CX, CY, CW, CH, FLinearColor(0.02f, 0.02f, 0.02f, 0.9f));
        Text(Tracker->bTrackerLive ? TEXT("no camera preview") : TEXT("tracker not running"), CX + CW / 2, CY + CH / 2 - 12 * U, 17, Cream, true, false);
        Text(Tracker->bTrackerLive ? TEXT("(tracker started with --no-preview)") : TEXT("python tracker/run.py --no-window"), CX + CW / 2, CY + CH / 2 + 14 * U, 13, Brass, true, false);
    }
    const FLinearColor Dot = Tracker->bTrackerLive ? FLinearColor(0.2f, 0.9f, 0.3f) : Brass;
    Box(CX + 4 * U, CY + CH + 9 * U, 14 * U, 14 * U, Dot);
    Text(Tracker->bTrackerLive ? TEXT("WEBCAM TRACKER") : TEXT("MOUSE MODE   LMB fire  R reload  A/D lean  S duck  H holster"), CX + 26 * U, CY + CH + 6 * U, Tracker->bTrackerLive ? 15 : 10, Cream, false, false);

    if (Tracker->bTrackerLive)
    {
        Text(Tracker->bFingerMode ? TEXT("SPACE recenter    P aim: ON FINGER") : TEXT("SPACE recenter    P aim: from centre"), CX + CW + 18 * U, CY + CH + 6 * U, 13, Cream, false);
    }
    if (FPlatformTime::Seconds() - Tracker->RecenteredAt < 1.2)
    {
        Text(TEXT("RECENTERED"), W * 0.5f, H * 0.62f, 44, Brass);
    }
    if (Tracker->NoPersonSeconds > 1.0f)
    {
        Box(0, H * 0.42f, W, H * 0.16f, FLinearColor(0, 0, 0, 0.6f));
        Text(TEXT("STEP INTO FRAME"), W * 0.5f, H * 0.5f, 80, Cream);
    }
}
