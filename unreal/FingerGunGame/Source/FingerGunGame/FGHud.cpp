#include "FGHud.h"

#include "CanvasItem.h"
#include "EngineUtils.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "FGBandit.h"
#include "FGGameState.h"
#include "FGPlayerController.h"
#include "FGPlayerState.h"
#include "FGSession.h"
#include "FGTrackerInput.h"
#include "FGTrainPlayer.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    const FLinearColor Cream(0.93f, 0.85f, 0.68f);
    const FLinearColor Ink(0.10f, 0.06f, 0.03f);
    const FLinearColor Brass(0.95f, 0.68f, 0.22f);
    const FLinearColor Blood(0.75f, 0.08f, 0.05f);
    const FLinearColor Amber(1.0f, 0.62f, 0.05f);

    FBox2D RideAgainButton() { return FBox2D(FVector2D(0.36, 0.76), FVector2D(0.64, 0.88)); }
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
    if (!Canvas) { return; }
    const float W = Canvas->SizeX, H = Canvas->SizeY;
    U = H / 1080.0f;
    const AFGGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFGGameState>() : nullptr;
    const AFGTrainPlayer* Player = Cast<AFGTrainPlayer>(GetOwningPawn());
    if (!GS || !Player)
    {
        Box(0, 0, W, H, FLinearColor(0, 0, 0, 0.7f));
        Text(TEXT("RIDING INTO TOWN..."), W * 0.5f, H * 0.5f, 48, Cream);
        return;
    }
    const AFGPlayerState* Me = Player->GetPlayerState<AFGPlayerState>();
    const AFGTrainPlayer* Partner = nullptr;
    for (const APlayerState* PS : GS->PlayerArray)
    {
        const AFGTrainPlayer* P = GS->PawnOf(PS);
        if (P && P != Player) { Partner = P; }
    }
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
    if (Player->Hats == 1 && !Player->bDowned)
    {
        const float Pulse = 0.12f + 0.08f * FMath::Sin(Now * 5.0f);
        Box(0, 0, W * 0.06f, H, FLinearColor(Blood.R, 0, 0, Pulse));
        Box(W * 0.94f, 0, W * 0.06f, H, FLinearColor(Blood.R, 0, 0, Pulse));
    }

    const AFGPlayerController* MenuPC = Cast<AFGPlayerController>(GetOwningPlayerController());
    if (GS->Phase == EFGPhase::Result)
    {
        DrawPoster(GS, Player, Me, Partner, Now);
    }
    else if (MenuPC && MenuPC->IsMenuOpen())
    {
        DrawMenu(MenuPC, Tracker, W, H, Now);
        DrawCoop(GS, W, H, Now);
    }
    else
    {
        // ---- prompts
        if (GS->bStayDown)
        {
            Text(TEXT("STAY DOWN!"), W * 0.5f, H * 0.20f, 80, Brass);
        }
        else if (GS->LeanWarning != 0)
        {
            const float Blink = FMath::Fmod(Now * 3.0f, 1.0f) < 0.6f ? 1.0f : 0.3f;
            const bool bRight = GS->LeanWarning > 0;
            Text(bRight ? TEXT("LEAN RIGHT  >>>") : TEXT("<<<  LEAN LEFT"), W * (bRight ? 0.66f : 0.34f), H * 0.30f, 84, FLinearColor(Brass.R, Brass.G, Brass.B, Blink));
        }
        else if (GS->bDuckWarning)
        {
            const float Blink = FMath::Fmod(Now * 3.0f, 1.0f) < 0.6f ? 1.0f : 0.3f;
            Text(TEXT("DUCK!"), W * 0.5f, H * 0.30f, 110, FLinearColor(Brass.R, Brass.G, Brass.B, Blink));
            Text(TEXT("v  v  v"), W * 0.5f, H * 0.41f, 50, FLinearColor(Brass.R, Brass.G, Brass.B, Blink));
        }
        else if (!GS->Prompt.IsEmpty())
        {
            const bool bBig = GS->Prompt == TEXT("DRAW!");
            Text(GS->Prompt, W * 0.5f, H * (GS->Phase == EFGPhase::Title ? 0.40f : 0.17f), bBig ? 150 : 64, bBig ? Blood : Cream);
        }
        // What to do with your own hands: depends on whether this player has a webcam tracker or the keys.
        FString Sub = GS->SubPrompt;
        if (GS->Prompt == TEXT("HOLSTER")) { Sub = Tracker->bTrackerLive ? TEXT("drop your gun hand to your hip") : TEXT("hold H"); }
        if (GS->Phase != EFGPhase::Title && Player->IsEmpty()) { Sub = Tracker->bTrackerLive ? TEXT("EMPTY!  SLAP YOUR GUN HAND TO RELOAD") : TEXT("EMPTY!  PRESS R TO RELOAD"); }
        if (!Sub.IsEmpty())
        {
            Text(Sub, W * 0.5f, H * (GS->Phase == EFGPhase::Title ? 0.50f : 0.245f), 30, Brass);
        }
        if (GS->Phase == EFGPhase::Title)
        {
            Text(TEXT("RED DEAD DIMENSION"), W * 0.5f, H * 0.22f, 96, Brass);
        }

        // ---- hats (health), cylinder (ammo), score
        for (int32 i = 0; i < 3; ++i)
        {
            Hat(W - (3 - i) * 95 * U - 30 * U, H * 0.06f + 8 * U, 56 * U, i < Player->Hats ? Cream : FLinearColor(0, 0, 0, 0.45f));
        }
        if (GS->bVersus && Partner)
        {
            // A 1v1: rounds won, yours first.
            const AFGPlayerState* Them = Partner->GetPlayerState<AFGPlayerState>();
            Text(FString::Printf(TEXT("YOU %d  -  %d THEM"), Me ? Me->Points : 0, Them ? Them->Points : 0), 40 * U, H * 0.06f + 10 * U, 40, Brass, false);
        }
        else
        {
            Text(FString::Printf(TEXT("$%d"), GS->Score), 40 * U, H * 0.06f + 10 * U, 40, Brass, false);
        }
        if (GS->Phase == EFGPhase::Ride || GS->Phase == EFGPhase::Showdown)
        {
            Text(FString::Printf(TEXT("%.1f km    %d mph    lap %d"), GS->DistanceM / 1000.0, int32(GS->Clock.Speed * 2.237f), GS->Lap + 1), 40 * U, H * 0.06f + 62 * U, 18, Cream, false);
        }
        if (Partner)
        {
            DrawPartner(Partner, W, H);
        }
        const float Banner = GS->BannerLeft();
        if (Banner > 0.0f)
        {
            // The host names players; each screen says it to its own player: "YOU WIN THE ROUND", "THEY DREW EARLY".
            FString Line = GS->Banner;
            if (GS->bVersus && Me && Partner && Partner->GetPlayerState())
            {
                const FString Mine = Me->GetPlayerName().ToUpper(), Theirs = Partner->GetPlayerState()->GetPlayerName().ToUpper();
                for (const TCHAR* Verb : { TEXT(" WINS"), TEXT(" TAKES") })
                {
                    Line.ReplaceInline(*(Mine + Verb), *(FString(TEXT("YOU")) + FString(Verb).LeftChop(1)), ESearchCase::CaseSensitive);
                    Line.ReplaceInline(*(Theirs + Verb), *(FString(TEXT("THEY")) + FString(Verb).LeftChop(1)), ESearchCase::CaseSensitive);
                }
                Line.ReplaceInline(*(Mine + TEXT(" DREW")), TEXT("YOU DREW"), ESearchCase::CaseSensitive);
                Line.ReplaceInline(*(Theirs + TEXT(" DREW")), TEXT("THEY DREW"), ESearchCase::CaseSensitive);
            }
            Text(Line, W * 0.5f, H * 0.33f, 72, FLinearColor(Brass.R, Brass.G, Brass.B, FMath::Min(1.0f, Banner)));
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

        // ---- incoming shot: a marker on the barrel that is about to fire, closing in as the 0.7 s run out.
        // Red when it is aimed at you, amber when it is your partner it wants.
        if (APlayerController* PC = GetOwningPlayerController())
        {
            for (TActorIterator<AFGBandit> It(GetWorld()); It; ++It)
            {
                const AFGBandit* B = *It;
                const float Warn = B->Warning();
                FVector2D At;
                if (Warn <= 0.0f || !PC->ProjectWorldLocationToScreen(B->MuzzleLocation(), At)) { continue; }
                const bool bOn = FMath::Fmod(Now * (5.0f + 9.0f * Warn), 1.0f) < 0.55f;
                const bool bMine = !B->Target || B->Target == Player || B->Spec.Kind == EFGBanditKind::Boss;
                const FLinearColor Col = bMine ? FLinearColor(1.0f, 0.05f, 0.03f, bOn ? 1.0f : 0.35f) : FLinearColor(Amber.R, Amber.G, Amber.B, bOn ? 0.8f : 0.25f);
                Ring(At, (70.0f - 48.0f * Warn) * U, (bMine ? 5 : 3) * U, Col, 24);
                Box(At.X - 7 * U, At.Y - 7 * U, 14 * U, 14 * U, Col);
            }
        }

        // ---- crosshair
        const bool bShow = GS->bCrosshairVisible && Tracker->State.bAimValid && !Player->bDowned;
        if (bShow)
        {
            const FVector2D C(Player->AssistedAim.X * W, Player->AssistedAim.Y * H);
            const FLinearColor Col = Player->HitMarker > 0.0f ? Blood : Cream;
            Ring(C, (38 + Player->HitMarker * 14) * U, 5 * U, Col, 36);
            Ring(C, (39 + Player->HitMarker * 14) * U + 3 * U, 2 * U, FLinearColor(0, 0, 0, 0.6f), 36);
            Box(C.X - 5 * U, C.Y - 5 * U, 10 * U, 10 * U, Col);
        }
        if (bShow && Player->IsDual() && Tracker->bTrackerLive)
        {
            // second gun: same ring in brass, so the two can be told apart
            const FVector2D C2(Player->AssistedAim2.X * W, Player->AssistedAim2.Y * H);
            Ring(C2, 38 * U, 5 * U, Brass, 36);
            Ring(C2, 42 * U, 2 * U, FLinearColor(0, 0, 0, 0.6f), 36);
            Box(C2.X - 5 * U, C2.Y - 5 * U, 10 * U, 10 * U, Brass);
        }
        else if (Player->HitMarker > 0.0f && !Tracker->bTrackerLive)
        {
            const FVector2D C(Tracker->State.AimX * W, Tracker->State.AimY * H);
            Ring(C, (38 + Player->HitMarker * 14) * U, 5 * U, Blood, 36);
        }

        if (Player->bDowned)
        {
            Box(0, H * 0.43f, W, H * 0.15f, FLinearColor(0, 0, 0, 0.55f));
            Text(TEXT("YOU'RE DOWN"), W * 0.5f, H * 0.475f, 64, Blood);
            Text(TEXT("hang on: a headshot from your partner brings you back"), W * 0.5f, H * 0.54f, 24, Cream);
        }
        DrawCoop(GS, W, H, Now);
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

void AFGHud::DrawPartner(const AFGTrainPlayer* Partner, float W, float H)
{
    // Top left, under the score: who you are riding with and how many hats they have left.
    const APlayerState* PS = Partner->GetPlayerState();
    const float X = 40 * U, Y = H * 0.06f + 92 * U;
    const AFGGameState* GS = GetWorld()->GetGameState<AFGGameState>();
    const bool bVersus = GS && GS->bVersus;
    Text(FString::Printf(TEXT("%s  (%s)"), bVersus ? TEXT("OPPONENT") : TEXT("PARTNER"), PS ? *PS->GetPlayerName().ToUpper() : TEXT("")), X, Y, 16, Cream, false);
    if (Partner->bDowned)
    {
        const bool bOn = FMath::Fmod(GetWorld()->GetRealTimeSeconds() * 2.0f, 1.0f) < 0.6f;
        Text(bVersus ? TEXT("DOWN") : TEXT("DOWN - HEADSHOT TO REVIVE"), X, Y + 24 * U, 16, FLinearColor(Blood.R, Blood.G, Blood.B, bOn ? 1.0f : 0.5f), false);
    }
    else
    {
        for (int32 i = 0; i < 3; ++i)
        {
            Hat(X + i * 46 * U, Y + 22 * U, 28 * U, i < Partner->Hats ? Cream : FLinearColor(0, 0, 0, 0.45f));
        }
    }
    // And over their head in the world, so they are never mistaken for a bandit.
    FVector2D At;
    if (APlayerController* PC = GetOwningPlayerController(); PC && !(GS && GS->BannerLeft() > 0.0f) && PC->ProjectWorldLocationToScreen(Partner->GetActorLocation() + FVector(0, 0, 150.0f), At))
    {
        Text(bVersus ? TEXT("OPPONENT") : (Partner->bDowned ? TEXT("PARTNER  (DOWN)") : TEXT("PARTNER")), At.X, At.Y, 18, Partner->bDowned ? Blood : Amber);
    }
}

void AFGHud::DrawCoop(const AFGGameState* GS, float W, float H, float Now)
{
    // Hosting and joining. Only before the train leaves (or on the poster), and only in solo.
    const AFGPlayerController* PC = Cast<AFGPlayerController>(GetOwningPlayerController());
    const UFGSession* Session = UFGSession::Get(this);
    if (Session && !Session->LastError.IsEmpty() && FPlatformTime::Seconds() - Session->LastErrorAt < 10.0)
    {
        Text(Session->LastError, W * 0.5f, H * 0.84f, 28, Blood);
    }
    if (GS->bWaitingForPartner)
    {
        Text(FString::Printf(TEXT("they pick JOIN A PARTNER and type   %s"), *UFGSession::LocalAddress()), W * 0.5f, H * 0.52f, 34, Brass);
        Text(TEXT("same Wi-Fi: that address.   over the internet: your public IP, with UDP port 7777 forwarded to this computer"), W * 0.5f, H * 0.58f, 18, Cream);
        Text(GS->bVersus ? TEXT("1v1 DUEL      ESC  back to the menu") : TEXT("ENTER  ride alone instead      ESC  back to the menu"), W * 0.5f, H * 0.66f, 22, Cream);
        return;
    }
    if (!PC) { return; }
    if (PC->bTypingAddress)
    {
        Box(W * 0.25f, H * 0.62f, W * 0.5f, H * 0.17f, FLinearColor(Ink.R, Ink.G, Ink.B, 0.9f));
        Text(TEXT("JOIN A PARTNER'S RIDE"), W * 0.5f, H * 0.655f, 26, Brass);
        const bool bCursor = FMath::Fmod(Now * 2.0f, 1.0f) < 0.5f;
        Text(PC->Address + (bCursor ? TEXT("_") : TEXT(" ")), W * 0.5f, H * 0.71f, 44, Cream);
        Text(TEXT("type the address their screen shows    ENTER join    ESC cancel"), W * 0.5f, H * 0.76f, 16, Cream);
        return;
    }
    if (Session && !Session->JoiningAddress.IsEmpty() && GetNetMode() == NM_Standalone)
    {
        Text(FString::Printf(TEXT("CONNECTING TO %s ..."), *Session->JoiningAddress), W * 0.5f, H * 0.70f, 30, Brass);
        return;
    }
}

void AFGHud::DrawMenu(const AFGPlayerController* PC, const UFGTrackerInput* Tracker, float W, float H, float Now)
{
    Box(0, 0, W, H, FLinearColor(0, 0, 0, 0.35f));
    Text(TEXT("RED DEAD DIMENSION"), W * 0.5f, H * 0.17f, 96, Brass);
    Text(TEXT("a finger-gun train shooter.  one player, or two on two computers"), W * 0.5f, H * 0.255f, 22, Cream);
    for (int32 i = 0; i < AFGPlayerController::MenuItemCount; ++i)
    {
        const FBox2D B = AFGPlayerController::MenuBox(i);
        const bool bOn = i == PC->MenuIndex;
        const float X = B.Min.X * W, Y = B.Min.Y * H, BW = (B.Max.X - B.Min.X) * W, BH = (B.Max.Y - B.Min.Y) * H;
        Box(X - 4 * U, Y - 4 * U, BW + 8 * U, BH + 8 * U, FLinearColor(Ink.R, Ink.G, Ink.B, 0.85f));
        Box(X, Y, BW, BH, bOn ? Brass : FLinearColor(0.22f, 0.15f, 0.09f, 0.85f));
        Text(FString::Printf(TEXT("%d    %s"), i + 1, AFGPlayerController::MenuLabel(i)), W * 0.5f, Y + BH * 0.36f, 32, bOn ? Ink : Cream, true, !bOn);
        Text(AFGPlayerController::MenuNote(i), W * 0.5f, Y + BH * 0.76f, 14, bOn ? Ink : FLinearColor(Cream.R, Cream.G, Cream.B, 0.8f), true, false);
    }
    Text(TEXT("aim with your finger gun (or the mouse) and shoot to pick        or 1-4, or the arrow keys and Enter"), W * 0.5f, H * 0.79f, 17, Cream);
    // A crosshair to pick with.
    const FVector2D C(Tracker->State.AimX * W, Tracker->State.AimY * H);
    Ring(C, 30 * U, 4 * U, Cream, 32);
    Box(C.X - 4 * U, C.Y - 4 * U, 8 * U, 8 * U, Cream);
}

void AFGHud::DrawPoster(const AFGGameState* GS, const AFGTrainPlayer* Player, const AFGPlayerState* Me, const AFGTrainPlayer* Partner, float Now)
{
    const float W = Canvas->SizeX, H = Canvas->SizeY;
    const bool bTwo = Partner != nullptr;
    const float PW = (bTwo ? 760 : 620) * U, PH = 800 * U, PX = (W - PW) * 0.5f, PY = (H - PH) * 0.5f;
    const float In = FMath::Clamp(GS->ResultTime() / 0.6f, 0.0f, 1.0f);
    Box(0, 0, W, H, FLinearColor(0, 0, 0, 0.45f * In));
    Box(PX - 8 * U, PY - 8 * U + (1 - In) * H, PW + 16 * U, PH + 16 * U, Ink);
    Box(PX, PY + (1 - In) * H, PW, PH, Cream);
    if (In < 1.0f) { return; }

    const float CX = W * 0.5f;
    const AFGPlayerState* Them = Partner ? Partner->GetPlayerState<AFGPlayerState>() : nullptr;
    auto Draw = [](int32 Ms) { return Ms >= 0 ? FString::Printf(TEXT("%d ms"), Ms) : FString(TEXT("--")); };
    const FBox2D B = RideAgainButton();
    const float Pulse = 0.85f + 0.15f * FMath::Sin(Now * 4.0f);
    const bool bWaiting = Me && Me->bVotedRide && Them && !Them->bVotedRide;
    if (GS->bVersus)
    {
        // The duel's poster: who won, the score, and who drew faster.
        const int32 Mine = Me ? Me->Points : 0, Theirs = Them ? Them->Points : 0;
        Text(TEXT("THE DUEL"), CX, PY + 70 * U, 40, Ink, true, false);
        Box(PX + 40 * U, PY + 115 * U, PW - 80 * U, 4 * U, Ink);
        Text(Mine > Theirs ? TEXT("YOU WIN") : TEXT("YOU LOSE"), CX, PY + 185 * U, 64, Blood, true, false);
        Text(FString::Printf(TEXT("%d  -  %d"), Mine, Theirs), CX, PY + 265 * U, 40, Ink, true, false);
        float RY = PY + 335 * U;
        const float C1 = PX + PW * 0.56f, C2 = PX + PW * 0.80f;
        Text(TEXT("YOU"), C1, RY, 22, Blood, true, false);
        Text(TEXT("THEM"), C2, RY, 22, Blood, true, false);
        RY += 40 * U;
        auto Row = [&](const FString& L, const FString& A, const FString& Bv)
        {
            Text(L, PX + 60 * U, RY - 12 * U, 22, Ink, false, false);
            Text(A, C1, RY, 22, Ink, true, false);
            Text(Bv, C2, RY, 22, Ink, true, false);
            RY += 38 * U;
        };
        Row(TEXT("ROUNDS WON"), FString::FromInt(Mine), FString::FromInt(Theirs));
        Row(TEXT("BEST DRAW"), Draw(Me ? Me->BestDrawMs : -1), Draw(Them ? Them->BestDrawMs : -1));
        Row(TEXT("DREW EARLY"), FString::FromInt(Me ? Me->Fouls : 0), FString::FromInt(Them ? Them->Fouls : 0));
        Row(TEXT("SHOTS FIRED"), FString::FromInt(Me ? Me->ShotsFired : 0), FString::FromInt(Them ? Them->ShotsFired : 0));
        Box(B.Min.X * W, B.Min.Y * H, (B.Max.X - B.Min.X) * W, (B.Max.Y - B.Min.Y) * H, FLinearColor(Blood.R * Pulse, Blood.G, Blood.B, 1.0f));
        Text(bWaiting ? TEXT("WAITING FOR THEM") : TEXT("SHOOT TO DUEL AGAIN"), W * 0.5f, (B.Min.Y + B.Max.Y) * 0.5f * H, 24, Cream, true, false);
        Text(TEXT("any shot, anywhere  (or slap, or Space)      M  main menu"), W * 0.5f, B.Max.Y * H + 26 * U, 16, Cream, true, true);
        return;
    }
    Text(TEXT("END OF THE LINE"), CX, PY + 70 * U, 40, Ink, true, false);
    Box(PX + 40 * U, PY + 115 * U, PW - 80 * U, 4 * U, Ink);
    Text(GS->Rank(), CX, PY + 185 * U, 58, Blood, true, false);
    Text(FString::Printf(TEXT("$%d REWARD"), GS->Score), CX, PY + 265 * U, 34, Ink, true, false);
    float Y = PY + 325 * U;
    if (!bTwo)
    {
        auto Row = [&](const FString& L, const FString& R)
        {
            Text(L, PX + 70 * U, Y, 24, Ink, false, false);
            Text(R, PX + PW - 230 * U, Y, 24, Ink, false, false);
            Y += 42 * U;
        };
        Row(TEXT("DISTANCE"), FString::Printf(TEXT("%.1f km"), GS->DistanceM / 1000.0));
        Row(TEXT("BOSSES"), FString::FromInt(GS->BossesBeaten));
        Row(TEXT("BANDITS"), FString::FromInt(GS->Kills));
        Row(TEXT("HEADSHOTS"), FString::FromInt(GS->Headshots));
        Row(TEXT("ACCURACY"), FString::Printf(TEXT("%d%%"), int32((Me ? Me->Accuracy() : 0.0f) * 100.0f)));
        Row(TEXT("DODGES"), FString::FromInt(GS->Dodges));
        Row(TEXT("BEST DRAW"), Draw(GS->BestDrawMs));
    }
    else
    {
        // The posse's totals, then a column each.
        Text(FString::Printf(TEXT("%.1f km    %d BOSS%s"), GS->DistanceM / 1000.0, GS->BossesBeaten, GS->BossesBeaten == 1 ? TEXT("") : TEXT("ES")), CX, Y, 24, Ink, true, false);
        Y += 44 * U;
        const float C1 = PX + PW * 0.56f, C2 = PX + PW * 0.80f;
        Text(TEXT("YOU"), C1, Y, 22, Blood, true, false);
        Text(TEXT("PARTNER"), C2, Y, 22, Blood, true, false);
        Y += 36 * U;
        auto Row = [&](const FString& L, const FString& A, const FString& B)
        {
            Text(L, PX + 60 * U, Y - 12 * U, 22, Ink, false, false);
            Text(A, C1, Y, 22, Ink, true, false);
            Text(B, C2, Y, 22, Ink, true, false);
            Y += 36 * U;
        };
        auto Num = [](const AFGPlayerState* PS, int32 AFGPlayerState::*Field) { return PS ? FString::FromInt(PS->*Field) : FString(TEXT("-")); };
        Row(TEXT("REWARD"), Me ? FString::Printf(TEXT("$%d"), Me->Points) : TEXT("-"), Them ? FString::Printf(TEXT("$%d"), Them->Points) : TEXT("-"));
        Row(TEXT("BANDITS"), Num(Me, &AFGPlayerState::Kills), Num(Them, &AFGPlayerState::Kills));
        Row(TEXT("HEADSHOTS"), Num(Me, &AFGPlayerState::Headshots), Num(Them, &AFGPlayerState::Headshots));
        Row(TEXT("ACCURACY"), FString::Printf(TEXT("%d%%"), int32((Me ? Me->Accuracy() : 0.0f) * 100.0f)), FString::Printf(TEXT("%d%%"), int32((Them ? Them->Accuracy() : 0.0f) * 100.0f)));
        Row(TEXT("DODGES"), Num(Me, &AFGPlayerState::Dodges), Num(Them, &AFGPlayerState::Dodges));
        Row(TEXT("BEST DRAW"), Draw(Me ? Me->BestDrawMs : -1), Draw(Them ? Them->BestDrawMs : -1));
        Row(TEXT("TIMES DOWN"), Num(Me, &AFGPlayerState::TimesDowned), Num(Them, &AFGPlayerState::TimesDowned));
    }
    Box(B.Min.X * W, B.Min.Y * H, (B.Max.X - B.Min.X) * W, (B.Max.Y - B.Min.Y) * H, FLinearColor(Blood.R * Pulse, Blood.G, Blood.B, 1.0f));
    // Cream on the cream poster is invisible, so the label has to fit inside the red button: keep it short.
    Text(bWaiting ? TEXT("WAITING FOR PARTNER") : TEXT("SHOOT TO RIDE AGAIN"), W * 0.5f, (B.Min.Y + B.Max.Y) * 0.5f * H, 24, Cream, true, false);
    Text(TEXT("any shot, anywhere  (or slap, or Space)      M  main menu"), W * 0.5f, B.Max.Y * H + 26 * U, 16, Cream, true, true);
}
