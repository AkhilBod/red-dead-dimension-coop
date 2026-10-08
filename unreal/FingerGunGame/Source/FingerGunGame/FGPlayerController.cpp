#include "FGPlayerController.h"

#include "FGCombat.h"
#include "FGGameState.h"
#include "FGIronHorseGameMode.h"
#include "FGSession.h"
#include "FGTrackerInput.h"
#include "FGTrainPlayer.h"
#include "Misc/ConfigCacheIni.h"

void AFGPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (!IsLocalController()) { return; }

    // Two people playing a webcam game on a laptop: spend the GPU on frame rate, not on ray tracing.
    // Measured on the M3 Air: 36 fps at 1600x900 with the project's Lumen + ray tracing defaults and nothing else
    // running, and the tracker still needs its share of the machine. Flat-shaded low-poly art loses nothing here.
    for (const TCHAR* Cmd : {
        TEXT("r.DynamicGlobalIlluminationMethod 0"), TEXT("r.ReflectionMethod 0"), TEXT("r.RayTracing.Enable 0"),
        TEXT("r.Lumen.HardwareRayTracing 0"), TEXT("r.RayTracing.Shadows 0"), TEXT("r.Shadow.Virtual.Enable 0"),
        TEXT("r.ScreenPercentage 70"), TEXT("r.AntiAliasingMethod 2"), TEXT("r.MotionBlurQuality 0"), TEXT("r.VolumetricFog 0"),
        TEXT("r.AmbientOcclusionLevels 0"), TEXT("r.DistanceFieldAO 0"), TEXT("r.DistanceFieldShadowing 0"),
        TEXT("r.Shadow.CSM.MaxCascades 2"), TEXT("r.Shadow.MaxResolution 1024"), TEXT("r.Shadow.DistanceScale 0.6"),
        TEXT("r.SkyLight.RealTimeReflectionCapture 0"), TEXT("r.BloomQuality 2"), TEXT("r.SceneColorFringeQuality 0"),
        TEXT("r.SetNearClipPlane 4"), TEXT("DisableAllScreenMessages"), TEXT("t.MaxFPS 60") })
    {
        ConsoleCommand(Cmd);
    }

    bShowMouseCursor = true;
    CurrentMouseCursor = EMouseCursor::Crosshairs;
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Mode);

    GConfig->GetString(TEXT("FingerGun"), TEXT("LastHost"), Address, GGameUserSettingsIni);
}

// ------------------------------------------------------------------ main menu

FBox2D AFGPlayerController::MenuBox(int32 Item)
{
    const float Y = 0.37f + Item * 0.1f;
    return FBox2D(FVector2D(0.32, Y), FVector2D(0.68, Y + 0.076));
}

int32 AFGPlayerController::MenuItemAt(FVector2D Aim)
{
    for (int32 i = 0; i < MenuItemCount; ++i)
    {
        if (MenuBox(i).IsInside(Aim)) { return i; }
    }
    return -1;
}

const TCHAR* AFGPlayerController::MenuLabel(int32 Item)
{
    switch (Item)
    {
    case Ride: return TEXT("RIDE");
    case HostRide: return TEXT("HOST A CO-OP RIDE");
    case HostVersus: return TEXT("HOST A 1v1 DUEL");
    case Join: return TEXT("JOIN A PARTNER");
    default: return TEXT("");
    }
}

const TCHAR* AFGPlayerController::MenuNote(int32 Item)
{
    switch (Item)
    {
    case Ride: return TEXT("on your own: cans, the bell, bandits, a boss, then round again harder");
    case HostRide: return TEXT("two players on two computers, the same train, the same bandits");
    case HostVersus: return TEXT("you against your partner: holster, wait, DRAW. First hit wins the round");
    case Join: return TEXT("your partner hosts a ride or a duel. Type the address their screen shows");
    default: return TEXT("");
    }
}

bool AFGPlayerController::IsMenuOpen() const
{
    const AFGGameState* GS = GetWorld()->GetGameState<AFGGameState>();
    return GS && GS->Phase == EFGPhase::Title && GetNetMode() == NM_Standalone && !bTypingAddress;
}

bool AFGPlayerController::MenuShot(FVector2D Aim)
{
    const int32 Item = MenuItemAt(Aim);
    if (Item < 0) { return false; }
    FGCombat::Sfx(GetWorld(), TEXT("shot_player"));
    Choose(Item);
    return true;
}

void AFGPlayerController::Choose(int32 Item)
{
    MenuIndex = Item;
    UFGSession* Session = UFGSession::Get(this);
    AFGIronHorseGameMode* GM = GetWorld()->GetAuthGameMode<AFGIronHorseGameMode>();
    switch (Item)
    {
    case Ride: if (GM) { GM->MenuRide(); } break;
    case HostRide: if (Session) { Session->Host(this, false); } break;
    case HostVersus: if (Session) { Session->Host(this, true); } break;
    case Join: bTypingAddress = true; break;
    default: break;
    }
}

void AFGPlayerController::TickMenu()
{
    // Whatever the finger gun (or the mouse) is over is the one picked; the keys move it too.
    if (const AFGTrainPlayer* Me = Cast<AFGTrainPlayer>(GetPawn()))
    {
        const FVector2D Aim(Me->Tracker->State.AimX, Me->Tracker->State.AimY);
        if (!Aim.Equals(LastMenuAim, 0.002))
        {
            LastMenuAim = Aim;
            const int32 Over = MenuItemAt(Aim);
            if (Over >= 0) { MenuIndex = Over; }
        }
    }
    if (WasInputKeyJustPressed(EKeys::Up)) { MenuIndex = (MenuIndex + MenuItemCount - 1) % MenuItemCount; }
    if (WasInputKeyJustPressed(EKeys::Down)) { MenuIndex = (MenuIndex + 1) % MenuItemCount; }
    static const FKey Numbers[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four };
    for (int32 i = 0; i < MenuItemCount; ++i)
    {
        if (WasInputKeyJustPressed(Numbers[i])) { Choose(i); return; }
    }
    if (WasInputKeyJustPressed(EKeys::Enter)) { Choose(MenuIndex); }
}

// ------------------------------------------------------------------ tick

void AFGPlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);
    const AFGGameState* GS = GetWorld()->GetGameState<AFGGameState>();
    if (!GS) { return; }

    if (bTypingAddress)
    {
        TickAddressEntry();
        return;
    }
    if (IsMenuOpen())
    {
        TickMenu();
        return;
    }
    if (GS->bWaitingForPartner)
    {
        if (WasInputKeyJustPressed(EKeys::Enter) && !GS->bVersus) { ServerRideAlone(); }
        if (WasInputKeyJustPressed(EKeys::Escape)) { if (UFGSession* Session = UFGSession::Get(this)) { Session->Leave(this); } }
    }
    if (GS->Phase == EFGPhase::Result && GS->ResultTime() > 1.0f)
    {
        if (WasInputKeyJustPressed(EKeys::Enter) || WasInputKeyJustPressed(EKeys::SpaceBar)) { ServerVoteRideAgain(); }
        // Back to the main menu. In co-op this leaves the ride (the other player carries on alone).
        if (WasInputKeyJustPressed(EKeys::M)) { if (UFGSession* Session = UFGSession::Get(this)) { Session->Leave(this); } }
    }
}

void AFGPlayerController::TickAddressEntry()
{
    static const FKey Digits[] = { EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
    static const FKey Pad[] = { EKeys::NumPadZero, EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree, EKeys::NumPadFour, EKeys::NumPadFive, EKeys::NumPadSix, EKeys::NumPadSeven, EKeys::NumPadEight, EKeys::NumPadNine };
    for (int32 i = 0; i < 10; ++i)
    {
        if (WasInputKeyJustPressed(Digits[i]) || WasInputKeyJustPressed(Pad[i])) { Address.AppendChar(TCHAR('0' + i)); }
    }
    if (WasInputKeyJustPressed(EKeys::Period) || WasInputKeyJustPressed(EKeys::Decimal)) { Address.AppendChar(TEXT('.')); }
    if (WasInputKeyJustPressed(EKeys::Semicolon) && (IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift))) { Address.AppendChar(TEXT(':')); }
    if (WasInputKeyJustPressed(EKeys::BackSpace)) { Address.LeftChopInline(1); }
    Address.LeftInline(40);
    if (WasInputKeyJustPressed(EKeys::Escape))
    {
        bTypingAddress = false;         // back to the menu
    }
    else if (WasInputKeyJustPressed(EKeys::Enter) && !Address.IsEmpty())
    {
        bTypingAddress = false;
        GConfig->SetString(TEXT("FingerGun"), TEXT("LastHost"), *Address, GGameUserSettingsIni);
        GConfig->Flush(false, GGameUserSettingsIni);
        if (UFGSession* Session = UFGSession::Get(this)) { Session->Join(this, Address); }
    }
}

void AFGPlayerController::ServerVoteRideAgain_Implementation()
{
    if (AFGIronHorseGameMode* GM = GetWorld()->GetAuthGameMode<AFGIronHorseGameMode>())
    {
        GM->VoteRideAgain(this);
    }
}

void AFGPlayerController::ServerRideAlone_Implementation()
{
    if (AFGIronHorseGameMode* GM = GetWorld()->GetAuthGameMode<AFGIronHorseGameMode>())
    {
        GM->RideAlone();
    }
}

void AFGPlayerController::ClientSfx_Implementation(FName Name, float Volume, float Pitch)
{
    FGCombat::Sfx(GetWorld(), Name, Volume, Pitch);
}
