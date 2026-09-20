#include "FGIronHorseGameMode.h"

#include "Components/AudioComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "EngineUtils.h"
#include "FGAssets.h"
#include "FGBandit.h"
#include "FGFx.h"
#include "FGHud.h"
#include "FGTarget.h"
#include "FGTrackerInput.h"
#include "FGTrain.h"
#include "FGTrainPlayer.h"
#include "FGWorldStreamer.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "AssetRegistry/AssetRegistryModule.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#endif
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
    constexpr float CruiseSpeed = 26.0f;            // m/s at the start of a run
    constexpr float TopSpeed = 46.0f;               // and the most it ever gets to
    constexpr float StageSeconds = 28.0f;           // played at 40: too long between bosses
    constexpr float StartDistance = 58.0f;          // metres: alongside the station platform
    constexpr float PlayerForwardCm = 300.0f;       // how far up the passenger car roof the player stands
    constexpr float AimAssistDegrees = 7.0f;        // how far off a shot may be and still hit
    constexpr float SideTrackCm = -700.0f;          // enemy line is 7 m to the driver's left

    const FVector2D CalibScreen[4] = { {0.22, 0.30}, {0.78, 0.30}, {0.78, 0.68}, {0.22, 0.68} };

    float AngleBetween(const FVector& Dir, const FVector& To)
    {
        return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Dir, To.GetSafeNormal()), -1.0, 1.0)));
    }
}

AFGIronHorseGameMode::AFGIronHorseGameMode()
{
    PrimaryActorTick.bCanEverTick = true;
    DefaultPawnClass = AFGTrainPlayer::StaticClass();
    HUDClass = AFGHud::StaticClass();
}

APawn* AFGIronHorseGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform&)
{
    // Whatever the level's PlayerStart says, the player stands on the passenger car roof at the origin facing up the train.
    const FTransform Roof(FRotator::ZeroRotator, FVector(PlayerForwardCm, 0.0f, AFGTrain::RoofCm + 96.0f));
    return Super::SpawnDefaultPawnAtTransform_Implementation(NewPlayer, Roof);
}

void AFGIronHorseGameMode::BeginPlay()
{
    Super::BeginPlay();
    UWorld* W = GetWorld();

    Preload();
    BuildSky();

    World = W->SpawnActor<AFGWorldStreamer>();
    World->Queue({ TEXT("Flat_A"), TEXT("Landmark_Station_A"), TEXT("Flat_B"), TEXT("Rocky_A"), TEXT("Landmark_WaterTower_A"), TEXT("Cactus_A"), TEXT("CurveL_A"), TEXT("Mesa_A"), TEXT("CurveR_A") });
    World->bAllowRandomLandmarks = false;
    World->SetDistance(StartDistance);

    BuildTrains();

    Player = Cast<AFGTrainPlayer>(UGameplayStatics::GetPlayerPawn(this, 0));

    if (USoundBase* Loop = FGAssets::Sound(TEXT("train_loop")))
    {
        TrainLoop = UGameplayStatics::SpawnSound2D(this, Loop, 0.0f, 1.0f, 0.0f, nullptr, false, false);
    }

    // Played with it on: the team preferred the train and the guns on their own. -FGMusic brings the loops back.
    bMusic = FParse::Param(FCommandLine::Get(), TEXT("FGMusic"));
    const TCHAR* Tracks[3] = { TEXT("music_day"), TEXT("music_night"), TEXT("music_boss") };
    for (int32 i = 0; bMusic && i < 3; ++i)
    {
        if (USoundBase* Track = FGAssets::Sound(Tracks[i]))
        {
            Music[i] = UGameplayStatics::SpawnSound2D(this, Track, 0.001f, 1.0f, 0.0f, nullptr, false, false);
        }
    }

    // Two people playing a webcam game on a laptop: spend the GPU on frame rate, not on ray tracing.
    if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
    {
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
            PC->ConsoleCommand(Cmd);
        }
    }
    // Test switches: -FGAuto plays by itself, -FGGod takes no damage, -FGShots=4 saves a screenshot every 4 s, -FGSkip=95 jumps into the ride.
    bAutoPlay = FParse::Param(FCommandLine::Get(), TEXT("FGAuto"));
    bGod = FParse::Param(FCommandLine::Get(), TEXT("FGGod"));
    bPerf = FParse::Param(FCommandLine::Get(), TEXT("FGPerf"));
    FParse::Value(FCommandLine::Get(), TEXT("FGShots="), ShotEvery);
    FParse::Value(FCommandLine::Get(), TEXT("FGSkip="), SkipTo);
    FParse::Value(FCommandLine::Get(), TEXT("FGLap="), TestLap);         // start on this lap
    FParse::Value(FCommandLine::Get(), TEXT("FGStage="), TestStage);     // 0 riders, 1 boarders, 2 other train, 3 boss
    SetPhase(EFGPhase::Title);
}

void AFGIronHorseGameMode::Preload()
{
    // Running from the editor binary, an asset is built the first time it is loaded (render data, skinning, distance
    // fields: 5-7 s each for a chunk or a cowboy) and the game thread waits for it. Mid-run that was a multi-second
    // freeze whenever a new chunk type or enemy type appeared, and again after the garbage collector dropped one.
    // So: load all of it now, wait for the builds, and hold on to it.
    const double Start = FPlatformTime::Seconds();
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.ScanPathsSynchronous({ TEXT("/Game/IronHorse") }, true);
    TArray<FAssetData> Assets;
    Registry.GetAssetsByPath(TEXT("/Game/IronHorse"), Assets, true);
    for (const FAssetData& Data : Assets)
    {
        const FName Class = Data.AssetClassPath.GetAssetName();
        if (Class == TEXT("StaticMesh") || Class == TEXT("SkeletalMesh") || Class == TEXT("AnimSequence") || Class == TEXT("SoundWave"))
        {
            if (UObject* Asset = Data.GetAsset()) { Preloaded.Add(Asset); }
        }
    }
#if WITH_EDITOR
    FAssetCompilingManager::Get().FinishAllCompilation();
#endif
    UE_LOG(LogTemp, Log, TEXT("IronHorse: preloaded %d assets in %.1f s"), Preloaded.Num(), FPlatformTime::Seconds() - Start);
}


void AFGIronHorseGameMode::TickTest(float DeltaTime)
{
    // -FGPerf: frame times to the log every 2 s (average fps, worst frame, how many frames went over 50 ms).
    if (bPerf)
    {
        const float Ms = FApp::GetDeltaTime() * 1000.0f;
        PerfSum += Ms; PerfWorst = FMath::Max(PerfWorst, Ms); PerfSlow += Ms > 50.0f; ++PerfFrames;
        if (PerfSum >= 2000.0f)
        {
            UE_LOG(LogTemp, Log, TEXT("IronHorse perf: %.0f fps, worst %.0f ms, %d slow frames, phase %d ride %.0fs, bandits %d"), PerfFrames * 1000.0f / PerfSum, PerfWorst, PerfSlow, int32(Phase), RideTime, Bandits.Num());
            PerfSum = PerfWorst = 0.0f; PerfSlow = PerfFrames = 0;
        }
    }
    if (ShotEvery > 0.0f)
    {
        ShotTimer -= DeltaTime;
        if (ShotTimer <= 0.0f)
        {
            ShotTimer = ShotEvery;
            FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / FString::Printf(TEXT("Screenshots/fg_%03d.png"), ShotIndex++)), true, false);
        }
    }
    if (!bAutoPlay) { return; }
    if (Phase == EFGPhase::Title && PhaseTime > 1.0f) { SetPhase(EFGPhase::Tutorial); }
    AutoTimer -= DeltaTime;
    if (AutoTimer > 0.0f) { return; }
    AutoTimer = 0.7f;
    if (Player->IsEmpty()) { Player->Tracker->OnReload.Broadcast(); return; }
    FVector Aim = FVector::ZeroVector;
    for (AFGTarget* T : Targets) { if (T && T->bActive) { Aim = T->Centre(); break; } }
    if (Aim.IsZero())
    {
        for (AFGBandit* B : Bandits)
        {
            if (B && !B->IsDead() && B->State != EFGBanditState::Entering) { FVector Chest, Head; B->AimPoints(Chest, Head); Aim = Chest; break; }
        }
    }
    if (Phase == EFGPhase::Showdown && ShowdownStep != 4) { return; }
    if (Phase == EFGPhase::Result) { if (ResultTime > 6.0f) { Player->Tracker->OnFire.Broadcast(RideAgainButton().GetCenter(), 0); } return; }
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    FVector2D Screen;
    int32 VW, VH;
    PC->GetViewportSize(VW, VH);
    if (!Aim.IsZero() && PC->ProjectWorldLocationToScreen(Aim, Screen) && VW > 0)
    {
        Player->Tracker->State.AimX = Screen.X / VW;
        Player->Tracker->State.AimY = Screen.Y / VH;
        Player->Tracker->OnFire.Broadcast(FVector2D(Screen.X / VW, Screen.Y / VH), 0);
    }
}

void AFGIronHorseGameMode::BuildSky()
{
    UWorld* W = GetWorld();
    if (TActorIterator<ADirectionalLight> It(W); It)
    {
        Sun = *It;      // the level brought its own lighting: leave it alone
        SunIntensity = Sun->GetLightComponent()->Intensity;
        for (TActorIterator<ASkyLight> SkyIt(W); SkyIt; ++SkyIt) { Sky = *SkyIt; }
        return;
    }
    // Golden hour: low warm sun ahead and to the left, long shadows, haze.
    bOwnSky = true;
    Sun = W->SpawnActor<ADirectionalLight>(FVector(0, 0, 3000), FRotator(-16.0f, 150.0f, 0.0f));
    UDirectionalLightComponent* SunComp = Cast<UDirectionalLightComponent>(Sun->GetLightComponent());
    SunComp->SetMobility(EComponentMobility::Movable);
    SunComp->SetIntensity(SunIntensity);
    SunComp->SetLightColor(FLinearColor(1.0f, 0.80f, 0.58f));
    SunComp->SetAtmosphereSunLight(true);
    SunComp->DynamicShadowDistanceMovableLight = 12000.0f;
    SunComp->DynamicShadowCascades = 2;

    W->SpawnActor<AActor>(ASkyAtmosphere::StaticClass(), FTransform::Identity);

    Sky = W->SpawnActor<ASkyLight>();
    Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    Sky->GetLightComponent()->SetRealTimeCapture(false);        // re-capturing the sky every frame cost several fps; it never changes
    Sky->GetLightComponent()->SetIntensity(1.3f);

    Fog = W->SpawnActor<AExponentialHeightFog>();
    UExponentialHeightFogComponent* FogComp = Fog->GetComponent();
    FogComp->SetFogDensity(0.018f);
    FogComp->SetFogHeightFalloff(0.08f);
    FogComp->SetFogInscatteringColor(FLinearColor(0.85f, 0.55f, 0.32f));
    FogComp->SetStartDistance(3000.0f);

    APostProcessVolume* PP = W->SpawnActor<APostProcessVolume>();
    PP->bUnbound = true;
    FPostProcessSettings& S = PP->Settings;
    S.bOverride_VignetteIntensity = true;       S.VignetteIntensity = 0.65f;
    S.bOverride_FilmGrainIntensity = true;      S.FilmGrainIntensity = 0.35f;
    S.bOverride_WhiteTemp = true;               S.WhiteTemp = 7300.0f;
    S.bOverride_ColorSaturation = true;         S.ColorSaturation = FVector4(0.92f, 0.90f, 0.86f, 1.0f);
    S.bOverride_ColorContrast = true;           S.ColorContrast = FVector4(1.08f, 1.08f, 1.08f, 1.0f);
    S.bOverride_BloomIntensity = true;          S.BloomIntensity = 0.9f;
    S.bOverride_MotionBlurAmount = true;        S.MotionBlurAmount = 0.35f;
    S.bOverride_AutoExposureMinBrightness = true; S.AutoExposureMinBrightness = 0.6f;
    S.bOverride_AutoExposureMaxBrightness = true; S.AutoExposureMaxBrightness = 1.6f;
}

void AFGIronHorseGameMode::BuildTrains()
{
    UWorld* W = GetWorld();
    Train = W->SpawnActor<AFGTrain>();
    Train->Build({ TEXT("Locomotive"), TEXT("Tender"), TEXT("Boxcar"), TEXT("PassengerCar"), TEXT("Flatcar_Crates"), TEXT("Caboose") }, 3, false);
    Train->Place(World, 0.0f);

    BanditTrain = W->SpawnActor<AFGTrain>();
    BanditTrain->Build({ TEXT("Locomotive"), TEXT("Tender"), TEXT("Boxcar"), TEXT("PassengerCar"), TEXT("Flatcar_Crates"), TEXT("Caboose") }, 3, true);
    BanditTrain->LateralCm = SideTrackCm;
    BanditTrain->Offset = -400.0;
    BanditTrain->SetActorHiddenInGame(true);

    // A lantern on the player's car, so the tunnel is dark but not black.
    Lantern = NewObject<UPointLightComponent>(Train);
    Lantern->SetMobility(EComponentMobility::Movable);
    Lantern->SetupAttachment(Train->GetRootComponent());
    Lantern->SetRelativeLocation(FVector(PlayerForwardCm + 250.0f, 0.0f, AFGTrain::RoofCm + 230.0f));
    Lantern->SetIntensityUnits(ELightUnits::Candelas);
    Lantern->SetIntensity(0.0f);
    Lantern->SetLightColor(FLinearColor(1.0f, 0.62f, 0.28f));
    Lantern->SetAttenuationRadius(2600.0f);
    Lantern->SetCastShadows(false);
    Lantern->RegisterComponent();
}

// ------------------------------------------------------------------ phases

void AFGIronHorseGameMode::SetPhase(EFGPhase NewPhase)
{
    Phase = NewPhase;
    PhaseTime = 0.0f;
    UE_LOG(LogTemp, Log, TEXT("IronHorse: phase %d, score %d"), int32(NewPhase), Score);
    UFGTrackerInput* Tracker = Player ? Player->Tracker.Get() : nullptr;
    switch (Phase)
    {
    case EFGPhase::Title:
        Prompt = TEXT("MAKE A FINGER GUN");
        SubPrompt = TEXT("point it at the screen");
        bCrosshairVisible = false;
        break;
    case EFGPhase::Calibrate:
        if (Tracker) { Tracker->SendCalibBegin(); }
        CalibIndex = 0;
        bCrosshairVisible = false;      // point at the bottle naturally, do not steer a cursor onto it
        Prompt = TEXT("SHOOT THE BOTTLE");
        SubPrompt = TEXT("drop your thumb to fire");
        SpawnCalibBottle();
        break;
    case EFGPhase::Tutorial:
        bCrosshairVisible = true;
        Prompt = TEXT("SHOOT THE CANS");
        SubPrompt = TEXT("");
        SpawnCans();
        break;
    case EFGPhase::Bell:
        Prompt = TEXT("SHOOT THE BELL TO DEPART");
        SubPrompt = TEXT("");
        SpawnBell();
        break;
    case EFGPhase::Ride:
        Prompt = TEXT("");
        SubPrompt = TEXT("");
        break;
    case EFGPhase::Showdown:
        EveryoneLeave();
        World->bStraightOnly = true;
        World->bAllowRandomLandmarks = false;
        World->ClearQueue();            // no tunnel, canyon or bridge turning up in the middle of a duel
        ShowdownStep = 0;
        ShowdownTimer = 3.0f;
        Prompt = TEXT("");
        break;
    case EFGPhase::Result:
        EveryoneLeave();
        ResultTime = 0.0f;
        bCrosshairVisible = true;
        Prompt = TEXT("");
        SubPrompt = TEXT("");
        break;
    }
}

FVector AFGIronHorseGameMode::ScreenToWorldPoint(FVector2D Screen, float DistanceCm) const
{
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    int32 W = 0, H = 0;
    FVector Origin, Dir;
    if (PC) { PC->GetViewportSize(W, H); }
    if (PC && W > 0 && PC->DeprojectScreenPositionToWorld(Screen.X * W, Screen.Y * H, Origin, Dir))
    {
        return Origin + Dir * DistanceCm;
    }
    return (Player ? Player->HeadLocation() : FVector::ZeroVector) + FVector(DistanceCm, (Screen.X - 0.5f) * DistanceCm, (0.5f - Screen.Y) * DistanceCm);
}

AFGTarget* AFGIronHorseGameMode::SpawnTarget(const FString& Folder, const FString& Mesh, const FTransform& At, float Radius)
{
    AFGTarget* T = GetWorld()->SpawnActor<AFGTarget>(AFGTarget::StaticClass(), At);
    if (!Mesh.IsEmpty()) { T->Mesh->SetStaticMesh(FGAssets::StaticMesh(Folder, Mesh)); }
    T->SetActorScale3D(At.GetScale3D());
    T->Radius = Radius;
    Targets.Add(T);
    return T;
}

void AFGIronHorseGameMode::SpawnCalibBottle()
{
    const FVector2D Screen = CalibScreen[CalibIndex];
    const FVector At = ScreenToWorldPoint(Screen, 750.0f);
    const float Scale = 2.6f;
    AFGTarget* Bottle = SpawnTarget(TEXT("props"), CalibIndex % 2 ? TEXT("SM_Bottle_Brown") : TEXT("SM_Bottle_Green"), FTransform(FRotator::ZeroRotator, At - FVector(0, 0, 15.0f * Scale), FVector(Scale)), 60.0f);
    Bottle->CentreOffset = FVector(0, 0, 15.0f * Scale);
    // Something for it to stand on: a post from the roof.
    const float PostHeight = Bottle->GetActorLocation().Z - AFGTrain::RoofCm;
    if (PostHeight > 5.0f)
    {
        UStaticMeshComponent* Post = NewObject<UStaticMeshComponent>(Bottle);
        Post->SetStaticMesh(FGAssets::StaticMesh(TEXT("props"), TEXT("SM_Crate_Small")));
        Post->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Post->SetUsingAbsoluteScale(true);
        Post->SetUsingAbsoluteLocation(true);
        Post->SetupAttachment(Bottle->Mesh);
        Post->RegisterComponent();
        Post->SetWorldScale3D(FVector(0.35f, 0.35f, PostHeight / 60.0f));
        Post->SetWorldLocation(FVector(Bottle->GetActorLocation().X, Bottle->GetActorLocation().Y, AFGTrain::RoofCm));
    }
    if (Player) { Player->Tracker->SendCalibTarget(Screen.X, Screen.Y); }
    Bottle->OnShot = [this](AFGTarget*)
    {
        Score += 10;
        PlaySfx(TEXT("glass"));
        if (++CalibIndex < 4) { SpawnCalibBottle(); }
        else { SetPhase(EFGPhase::Tutorial); }
    };
}

void AFGIronHorseGameMode::SpawnCans()
{
    // Two cans on the boxcar roof ahead: enough to learn the trigger, then the bell.
    CansLeft = 2;
    for (int32 i = 0; i < 2; ++i)
    {
        const FVector OnBoxcar(-450.0f + i * 40.0f, (i * 2 - 1) * 90.0f, AFGTrain::RoofCm);
        const FVector Base = (FTransform(OnBoxcar) * OnOwnCar(2)()).GetLocation();
        AFGTarget* Crate = SpawnTarget(TEXT("props"), TEXT("SM_Crate"), FTransform(FRotator(0, i * 17.0f, 0), Base), 0.0f);
        Crate->bActive = false;
        Crate->Anchor = OnOwnCar(2);
        Crate->Local = OnBoxcar;
        const float Scale = 3.2f;
        AFGTarget* Can = SpawnTarget(TEXT("props"), TEXT("SM_TinCan"), FTransform(FRotator::ZeroRotator, Base + FVector(0, 0, 90.0f), FVector(Scale)), 28.0f);
        Can->CentreOffset = FVector(0, 0, 5.5f * Scale);
        Can->OnShot = [this, Crate](AFGTarget*)
        {
            Score += 10;
            PlaySfx(TEXT("ricochet"));
            if (--CansLeft <= 0) { SetPhase(EFGPhase::Bell); }
        };
    }
}

void AFGIronHorseGameMode::SpawnBell()
{
    AFGTarget* Bell = SpawnTarget(TEXT(""), TEXT(""), FTransform(FVector(1200.0f, 420.0f, 300.0f)), 110.0f);
    Bell->bBreaks = false;
    USkeletalMeshComponent* Rig = World ? World->FindRig(TEXT("StationBell")) : nullptr;
    if (Rig)
    {
        // As modelled the bell hangs 2.5 m up, far below a player on the roof. A taller post puts it near eye level.
        const float Tall = 2.3f;
        Rig->SetRelativeScale3D(FVector(Tall));
        Bell->SetActorLocation(Rig->GetComponentLocation());
        Bell->CentreOffset = FVector(0, 0, 250.0f * Tall);
        Bell->Radius = 160.0f;
    }
    else
    {
        // No station in view: hang a start sign in the middle of the screen instead.
        Bell->Mesh->SetStaticMesh(FGAssets::StaticMesh(TEXT("props"), TEXT("SM_StartSign")));
        Bell->SetActorLocation(ScreenToWorldPoint(FVector2D(0.5, 0.45), 900.0f));
        Bell->SetActorRotation(FRotator(0, 90.0f, 0));
        Bell->CentreOffset = FVector(0, 0, 120.0f);
    }
    Bell->OnShot = [this, Rig](AFGTarget* T)
    {
        if (Rig)
        {
            if (UAnimSequence* Ring = FGAssets::Anim(TEXT("setpieces"), TEXT("SK_StationBell"), TEXT("ring"))) { Rig->PlayAnimation(Ring, false); }
        }
        T->bActive = false;
        T->SetLifeSpan(Rig ? 0.1f : 1.0f);
        Depart();
    };
}

void AFGIronHorseGameMode::Depart()
{
    PlaySfx(TEXT("bell"));
    PlaySfx(TEXT("whistle"));
    for (AFGTarget* T : Targets) { if (T && T->bActive == false && T->Radius == 0.0f) { T->SetLifeSpan(6.0f); } }
    RideTime = 0.0f;
    DistanceM = 0.0;
    World->bAllowRandomLandmarks = true;        // water towers and signal gantries turn up by themselves from here on
    BeginLap(TestLap);
    if (TestStage >= 0)
    {
        TrainSpeed = TargetSpeed();
        if (TestStage >= 3) { SetPhase(EFGPhase::Showdown); }
        else { NextStage(EFGStage(TestStage)); SpawnTimer = 2.0f; }
    }
}

// ------------------------------------------------------------------ the endless run

float AFGIronHorseGameMode::TargetSpeed() const { return FMath::Min(CruiseSpeed + 2.0f * Lap + RideTime / 60.0f * 2.5f, TopSpeed); }
int32 AFGIronHorseGameMode::MaxAlive() const { return FMath::Clamp(3 + Lap, 3, 6); }
int32 AFGIronHorseGameMode::TokenLimit() const { return Lap >= 2 ? 3 : 2; }
float AFGIronHorseGameMode::ShotFlight() const { return FMath::Max(0.45f, 0.7f - 0.05f * Lap); }
float AFGIronHorseGameMode::FireDelayScale() const { return FMath::Max(0.55f, 1.0f - 0.12f * Lap); }
float AFGIronHorseGameMode::SpawnEvery() const { return FMath::Max(1.2f, 2.7f - 0.4f * Lap); }

TFunction<FTransform()> AFGIronHorseGameMode::OnOwnCar(int32 CarIndex) const
{
    // Things on the cars ahead belong to those cars. Each car sits on its own bit of track, so on a curve the boxcar
    // swings away from straight ahead, and anything placed in plain world coordinates slid across its roof.
    return [this, CarIndex]() { return World->TrackWorld(Train->CarCentre(CarIndex), 0.0f); };
}

void AFGIronHorseGameMode::BeginLap(int32 NewLap)
{
    Lap = NewLap;
    Boss = nullptr;
    Phase = EFGPhase::Ride;
    PhaseTime = 0.0f;
    Prompt = SubPrompt = TEXT("");
    World->bStraightOnly = false;
    World->bAllowRandomLandmarks = true;
    NextStage(EFGStage::Riders);
    SpawnTimer = Lap == 0 ? 8.0f : 3.0f;
    if (Player->WeaponIndex != Lap % 4 || Lap > 0) { Player->SetWeapon(Lap); }      // a new gun every lap
    if (Lap > 0)
    {
        // The first boss falls at sunset. After him it is night, and after the next one morning, and so on.
        NightTarget = Lap % 2 ? 1.0f : 0.0f;
        if (NightTarget == 0.0f) { Dusk = 0.1f; }
        Banner = FString::Printf(TEXT("%s    %s"), NightTarget > 0.5f ? TEXT("NIGHT FALLS") : TEXT("DAWN"), Player->Weapon().Name);
        BannerTime = 3.5f;
    }
}

void AFGIronHorseGameMode::NextStage(EFGStage NewStage)
{
    Stage = NewStage;
    StageTime = 0.0f;
    StageFlags = 0;
    TrainTime = 0.0f;
    CrewSpawned = 0;
    bBanditTrainCrewed = false;
    UE_LOG(LogTemp, Log, TEXT("IronHorse: lap %d stage %d at %.0f s, %.0f m/s"), Lap, int32(Stage), RideTime, TrainSpeed);
}

void AFGIronHorseGameMode::SpawnBarrel(const FVector& Local, TFunction<FTransform()> Anchor)
{
    // Oil barrels: shoot one and everybody standing near it goes with it.
    int32 Live = 0;
    for (const AFGTarget* T : Targets) { Live += T && T->bExplosive && T->bActive; }
    if (Live >= 4) { return; }
    const float Scale = 1.25f;
    AFGTarget* Barrel = SpawnTarget(TEXT("props"), TEXT("SM_Barrel"), FTransform(FRotator::ZeroRotator, (FTransform(Local) * (Anchor ? Anchor() : FTransform::Identity)).GetLocation(), FVector(Scale)), 65.0f);
    Barrel->bExplosive = true;
    Barrel->CentreOffset = FVector(0, 0, 45.0f * Scale);
    Barrel->Anchor = Anchor;
    Barrel->Local = Local;
    Barrel->OnShot = [this](AFGTarget* T)
    {
        const FVector At = T->Centre();
        PlaySfx(TEXT("boom"));
        if (AFGFx* Boom = AFGFx::Spawn(GetWorld(), TEXT("fx"), TEXT("SM_MuzzleFlash_Big"), FTransform(FRotator::ZeroRotator, At, FVector(5.0f)), 0.35f, FVector::ZeroVector, 5.0f))
        {
            Boom->Glow(FLinearColor(3.0f, 1.4f, 0.4f), 0.4f);
            Boom->AddLight(FLinearColor(1.0f, 0.55f, 0.2f), 9000.0f, 6000.0f);
        }
        for (int32 i = 0; i < 3; ++i)
        {
            if (AFGFx* P = AFGFx::Spawn(GetWorld(), TEXT("fx"), TEXT("SM_Puff_Smoke"), FTransform(FRotator(0, i * 120.0f, 0), At + FVector(0, 0, i * 60.0f), FVector(1.2f)), 1.6f, FVector(-TrainSpeed * 60.0f, 0, 260.0f), 2.2f)) { P->Glow(FLinearColor(0.12f, 0.11f, 0.10f), 0.5f); }
        }
        Score += 25;
        const TArray<TObjectPtr<AFGBandit>> Near = Bandits;        // killing edits the list
        for (AFGBandit* B : Near)
        {
            if (B && !B->IsDead() && B != Boss && FVector::Dist(B->GetActorLocation(), At) < 520.0f)
            {
                FHitResult Blast;
                Blast.ImpactPoint = Blast.Location = At;
                UGameplayStatics::ApplyPointDamage(B, 1000.0f, (B->GetActorLocation() - At).GetSafeNormal(), Blast, Player->GetController(), Player, nullptr);
                Score += 50;
            }
        }
    };
}

// ------------------------------------------------------------------ tick

void AFGIronHorseGameMode::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (!Player || !World) { return; }
    PhaseTime += DeltaTime;
    if (!bSkyCaptured && Sky && GetWorld()->GetTimeSeconds() > 1.0f)
    {
        bSkyCaptured = true;        // once, after the atmosphere has rendered a few frames
        Sky->GetLightComponent()->RecaptureSky();
    }
    HitMarker = FMath::Max(0.0f, HitMarker - DeltaTime * 4.0f);
    InvulnerableFor = FMath::Max(0.0f, InvulnerableFor - DeltaTime);
    Bandits.RemoveAll([](const AFGBandit* B) { return !IsValid(B); });
    Targets.RemoveAll([](const AFGTarget* T) { return !IsValid(T); });

    // The train
    const bool bMoving = Phase == EFGPhase::Ride || Phase == EFGPhase::Showdown || (Phase == EFGPhase::Result && TrainSpeed > 0.0f);
    const float WantSpeed = !bMoving ? 0.0f : (Phase == EFGPhase::Result ? CruiseSpeed * 0.6f : TargetSpeed());
    TrainSpeed = FMath::FInterpConstantTo(TrainSpeed, WantSpeed, DeltaTime, 2.6f);
    World->SetDistance(World->GetDistance() + TrainSpeed * DeltaTime);
    if (Phase == EFGPhase::Ride || Phase == EFGPhase::Showdown) { DistanceM += TrainSpeed * DeltaTime; }
    BannerTime = FMath::Max(0.0f, BannerTime - DeltaTime);
    Train->Place(World, TrainSpeed);
    if (!BanditTrain->IsHidden()) { BanditTrain->Place(World, TrainSpeed); }
    Player->Rumble = FMath::Min(TrainSpeed / CruiseSpeed, 1.4f);
    if (TrainLoop) { TrainLoop->SetVolumeMultiplier(0.15f + 0.85f * Player->Rumble); TrainLoop->SetPitchMultiplier(0.6f + 0.5f * Player->Rumble); }

    // Music: day ride, night heist, or the duel. All three keep playing and only the volumes move, so the change
    // is a crossfade and never a restart.
    const int32 Wanted = Phase == EFGPhase::Showdown ? 2 : (NightTarget > 0.5f ? 1 : 0);
    for (int32 i = 0; i < 3; ++i)
    {
        const float Loud = Phase == EFGPhase::Result ? 0.25f : (Phase == EFGPhase::Ride || Phase == EFGPhase::Showdown ? 0.55f : 0.4f);
        MusicLevel[i] = FMath::FInterpConstantTo(MusicLevel[i], i == Wanted ? Loud : 0.0f, DeltaTime, 0.35f);
        if (Music[i]) { Music[i]->SetVolumeMultiplier(FMath::Max(MusicLevel[i], 0.001f)); }
    }

    // Steam from the stack
    SteamTimer -= DeltaTime;
    if (SteamTimer <= 0.0f)
    {
        SteamTimer = TrainSpeed > 1.0f ? 0.11f : 0.5f;
        if (USkeletalMeshComponent* Loco = Train->Car(0))
        {
            const FVector Stack = Loco->GetComponentTransform().TransformPosition(FVector(0.0f, 330.0f, 440.0f));
            if (AFGFx* Puff = AFGFx::Spawn(GetWorld(), TEXT("fx"), TEXT("SM_Puff_Steam"), FTransform(FRotator(0, FMath::FRandRange(0.f, 360.f), 0), Stack, FVector(0.3f)),
                1.8f, FVector(-TrainSpeed * 80.0f, FMath::FRandRange(-40.f, 40.f), 520.0f), 1.3f))
            {
                Puff->Glow(FLinearColor(0.75f, 0.72f, 0.68f), 0.45f);
            }
        }
    }

    switch (Phase)
    {
    case EFGPhase::Title:
        if (PhaseTime > 2.5f && Player->Tracker->State.bAimValid && Player->Tracker->State.bGunPose) { SetPhase(EFGPhase::Tutorial); }
        break;
    case EFGPhase::Calibrate:
    case EFGPhase::Tutorial:
    case EFGPhase::Bell:
        break;
    case EFGPhase::Ride:
        TickRide(DeltaTime);
        break;
    case EFGPhase::Showdown:
        TickShowdown(DeltaTime);
        break;
    case EFGPhase::Result:
        ResultTime += DeltaTime;
        break;
    }

    // Out of rounds: say so, whatever else is going on.
    if (Phase != EFGPhase::Result && Phase != EFGPhase::Title)
    {
        if (Player->IsEmpty()) { SubPrompt = Player->Tracker->bTrackerLive ? TEXT("EMPTY!  SLAP YOUR GUN HAND TO RELOAD") : TEXT("EMPTY!  PRESS R TO RELOAD"); }
        else if (SubPrompt.StartsWith(TEXT("EMPTY"))) { SubPrompt = TEXT(""); }
    }

    TickTest(DeltaTime);
    TickMagnet(DeltaTime);
    TickShots(DeltaTime);
    TickDuck();
    TickLeanObstacles(DeltaTime);
    TickAtmosphere(DeltaTime);
}

int32 AFGIronHorseGameMode::AliveBandits() const
{
    int32 N = 0;
    for (const AFGBandit* B : Bandits) { if (B && !B->IsDead() && B->State != EFGBanditState::Leaving) { ++N; } }
    return N;
}

void AFGIronHorseGameMode::EveryoneLeave()
{
    for (AFGBandit* B : Bandits) { if (B && B != Boss) { B->Leave(); } }
}

AFGBandit* AFGIronHorseGameMode::SpawnBandit(const FFGBanditSpec& Spec)
{
    AFGBandit* B = GetWorld()->SpawnActor<AFGBandit>(AFGBandit::StaticClass(), FTransform(FVector(0, 0, -5000)));
    B->Init(Spec, this);
    Bandits.Add(B);
    ++SpawnCount;
    return B;
}

void AFGIronHorseGameMode::TickRide(float DeltaTime)
{
    RideTime += DeltaTime;
    StageTime += DeltaTime;
    SpawnTimer -= DeltaTime;
    const bool bNarrow = World->MetresTo(TEXT("narrow")) == 0.0f || World->MetresTo(TEXT("dark")) == 0.0f || World->MetresTo(TEXT("trestle")) == 0.0f;
    auto Once = [this](int32 Bit, float At) { if (StageTime < At || (StageFlags & (1 << Bit))) { return false; } StageFlags |= 1 << Bit; return true; };

    // Tunnels are a ducking section, nothing else: nobody new shows up from 150 m out, and whoever is still
    // around clears off at the mouth. A bandit cannot ride or climb aboard inside a tunnel anyway.
    const float ToDark = World->MetresTo(TEXT("dark"));
    const bool bTunnelNear = ToDark >= 0.0f && ToDark < 150.0f;
    if (ToDark == 0.0f && !bInTunnel) { EveryoneLeave(); }
    bInTunnel = ToDark == 0.0f;
    if (bTunnelNear) { SpawnTimer = FMath::Max(SpawnTimer, 1.5f); }

    // Horses cannot cross a trestle or squeeze into a slot canyon. Riders rein in well before the edge and nobody
    // new rides up until it is behind us.
    float ToGap = -1.0f;
    for (const TCHAR* Kind : { TEXT("trestle"), TEXT("narrow") })
    {
        const float D = World->MetresTo(Kind);
        if (D >= 0.0f && (ToGap < 0.0f || D < ToGap)) { ToGap = D; }
    }
    const bool bGapNear = ToGap >= 0.0f && ToGap < 220.0f;
    if (ToGap >= 0.0f && ToGap < 120.0f)
    {
        for (AFGBandit* B : Bandits) { if (B && B->IsRider() && !B->IsDead()) { B->Leave(); } }
    }

    // Never a dead stretch: if there has been nobody to shoot at for a few seconds, whatever the stage is waiting
    // for (the other train to arrive, a crew used up), a rider comes up. On the right only while the left is a railway.
    QuietTime = AliveBandits() > 0 ? 0.0f : QuietTime + DeltaTime;
    if (QuietTime > 2.5f && !bNarrow && !bTunnelNear && !bGapNear && !(Stage == EFGStage::Riders && StageTime < 7.0f && Lap == 0))
    {
        QuietTime = 0.0f;
        const bool bLeftBusy = World->MetresTo(TEXT("side_track")) >= 0.0f && World->MetresTo(TEXT("side_track")) < 300.0f;
        FFGBanditSpec Spec;
        Spec.Kind = EFGBanditKind::Rider;
        const float Side = bLeftBusy || SpawnCount % 2 == 0 ? 1.0f : -1.0f;
        Spec.Slot = FVector(FMath::FRandRange(2400.0f, 3800.0f), Side * FMath::FRandRange(620.0f, 880.0f), 0.0f);
        Spec.Scale = 1.35f;
        Spec.HorseCoat = SpawnCount;
        Spec.FirstShotDelay = FMath::FRandRange(1.5f, 2.5f) * FireDelayScale();
        SpawnBandit(Spec);
    }

    // Set pieces are queued, not placed: the streamer lays them as soon as the joint rule allows, 450 m ahead,
    // so each one turns up 10-17 s after it is asked for, in the order asked.
    switch (Stage)
    {
    case EFGStage::Riders:
    {
        if (Once(1, 0.0f) && (Lap > 0 || StageTime > 0.0f))
        {
            // Through a town without stopping: a street either side, the station in the middle.
            // (Not the station on the first lap: we have only just left one.)
            World->Queue(Lap == 0 ? TArray<FString>{ TEXT("Flat_A+town"), TEXT("Flat_B+town") } : TArray<FString>{ TEXT("Flat_A+town"), TEXT("Landmark_Station_A"), TEXT("Flat_B+town") });
        }
        if (Once(0, 6.0f))
        {
            // A trestle bridge over a gulch. Riders drop back for it: there is nowhere to ride.
            World->Queue(Lap % 2 ? TArray<FString>{ TEXT("Gulch_A"), TEXT("Flat_B"), TEXT("Gulch_CurveR_A") } : TArray<FString>{ TEXT("Gulch_A") });
        }
        if (SpawnTimer <= 0.0f && AliveBandits() < MaxAlive() && !bNarrow && !bTunnelNear && !bGapNear)
        {
            SpawnTimer = StageTime < 8.0f && Lap == 0 ? 4.0f : SpawnEvery();
            FFGBanditSpec Spec;
            Spec.Kind = EFGBanditKind::Rider;
            const float Side = SpawnCount % 2 ? -1.0f : 1.0f;
            Spec.Slot = FVector(FMath::FRandRange(2400.0f, 3800.0f), Side * FMath::FRandRange(620.0f, 880.0f), 0.0f);
            Spec.Scale = 1.35f;        // out to the side they read small. Bigger and closer.
            Spec.Mesh = SpawnCount % 5 == 4 ? TEXT("SK_Gunslinger") : (SpawnCount % 3 == 2 ? TEXT("SK_Deputy") : TEXT("SK_Bandit"));
            Spec.HorseCoat = SpawnCount;
            Spec.FirstShotDelay = FMath::FRandRange(1.5f, 3.0f) * FireDelayScale();
            SpawnBandit(Spec);
        }
        if (StageTime > StageSeconds) { EveryoneLeave(); NextStage(EFGStage::Boarders); SpawnTimer = 2.0f; }
        break;
    }
    case EFGStage::Boarders:
    {
        if (Once(0, 0.0f))
        {
            World->Queue({ TEXT("CanyonDeep_Entry_A"), TEXT("CanyonDeep_Mid_A"), TEXT("CanyonDeep_CurveL_A"), TEXT("CanyonDeep_Mid_A"), TEXT("CanyonDeep_CurveR_A"), TEXT("CanyonDeep_Exit_A"), TEXT("Flat_A") });
        }
        if (Once(1, 6.0f))
        {
            // Straight tunnel pieces only: those are the ones the streamer widens.
            World->Queue({ TEXT("Tunnel_Entry_A"), TEXT("Tunnel_Mid_A"), TEXT("Tunnel_Mid_A"), TEXT("Tunnel_Mid_A"), TEXT("Tunnel_Exit_A"), TEXT("Rocky_A") });
        }
        if (Once(2, StageSeconds - 450.0f / FMath::Max(TrainSpeed, 10.0f) - 8.0f))
        {
            TArray<FString> Line = { TEXT("SideTrack_Start_A") };
            const TCHAR* Pattern[] = { TEXT("SideTrack_Mid_A"), TEXT("SideTrack_Mid_A"), TEXT("SideTrack_CurveL_A"), TEXT("SideTrack_Mid_A"), TEXT("SideTrack_CurveR_A"), TEXT("SideTrack_Mid_A") };
            const int32 Pieces = FMath::CeilToInt32((StageSeconds + 22.0f) * TargetSpeed() / 50.0f);
            for (int32 i = 0; i < Pieces; ++i) { Line.Add(Pattern[i % 6]); }
            Line.Add(TEXT("SideTrack_End_A"));
            World->Queue(Line);
        }
        if (SpawnTimer <= 0.0f && AliveBandits() < MaxAlive() && !bTunnelNear)
        {
            SpawnTimer = SpawnEvery();
            FFGBanditSpec Spec;
            // In the boxcar's own frame (its centre is 13.9 m up the train).
            static const FVector Slots[] = { {10, -45, 0}, {260, 50, 0}, {510, -35, 0}, {110, 55, 0}, {410, -50, 0} };
            Spec.Slot = Slots[SpawnCount % 5] + FVector(0, 0, AFGTrain::RoofCm);
            Spec.Anchor = OnOwnCar(2);
            // From the second lap one in four boarders brings dynamite.
            const bool bDynamite = Lap >= 1 && SpawnCount % 4 == 1;
            Spec.Kind = bDynamite ? EFGBanditKind::Dynamiter : EFGBanditKind::Boarder;
            Spec.Mesh = bDynamite ? TEXT("SK_Dynamiter") : (SpawnCount % 4 == 3 ? TEXT("SK_Heavy") : (SpawnCount % 2 ? TEXT("SK_Bandit") : TEXT("SK_Deputy")));
            Spec.Health = Spec.Mesh == TEXT("SK_Heavy") ? 50.0f : 25.0f;
            Spec.FirstShotDelay = FMath::FRandRange(1.0f, 2.2f) * FireDelayScale();
            if (SpawnCount % 3 == 0) { SpawnBarrel(Spec.Slot + FVector(-60.0f, Spec.Slot.Y > 0 ? -95.0f : 95.0f, 0.0f), OnOwnCar(2)); }
            SpawnBandit(Spec);
        }
        if (StageTime > StageSeconds) { EveryoneLeave(); NextStage(EFGStage::SecondTrain); }
        break;
    }
    case EFGStage::SecondTrain:
    {
        // The bandit train pulls alongside once there is a second line to run on, and drops back before it ends.
        const float ToSide = World->MetresTo(TEXT("side_track"));
        const bool bTimeUp = TrainTime > StageSeconds + 8.0f || (TrainTime > 8.0f && ToSide != 0.0f) || (BanditTrain->IsHidden() && StageTime > 35.0f);
        if (BanditTrain->IsHidden() && ToSide == 0.0f && !bTimeUp)
        {
            BanditTrain->SetActorHiddenInGame(false);
            BanditTrain->Offset = -260.0;
            PlaySfx(TEXT("whistle"), 0.7f, 0.8f);
        }
        if (!BanditTrain->IsHidden() && !bTimeUp)
        {
            TrainTime += DeltaTime;
            BanditTrain->Offset = FMath::FInterpTo(BanditTrain->Offset, 24.0, DeltaTime, 0.55f);
            auto OnCar = [this](int32 CarIndex) { return [this, CarIndex]() { return World->TrackWorld(BanditTrain->Offset + BanditTrain->CarCentre(CarIndex), BanditTrain->LateralCm); }; };
            if (!bBanditTrainCrewed && BanditTrain->Offset > -10.0)
            {
                bBanditTrainCrewed = true;
                SpawnTimer = 0.0f;
                SpawnBarrel(FVector(-260.0f, 0.0f, AFGTrain::RoofCm), OnCar(3));
                SpawnBarrel(FVector(200.0f, 0.0f, AFGTrain::RoofCm), OnCar(2));
            }
            // A crew, not a fountain: so many per visit, and they climb up the far side.
            if (bBanditTrainCrewed && SpawnTimer <= 0.0f && AliveBandits() < MaxAlive() + 1 && CrewSpawned < 10 + 3 * Lap)
            {
                SpawnTimer = SpawnEvery();
                struct FSeat { int32 Car; FVector Local; };
                static const FSeat Seats[] = { {3, {350, 0, 0}}, {2, {-150, 0, 0}}, {3, {-400, 30, 0}}, {2, {300, -20, 0}}, {4, {0, 0, -130}}, {3, {0, -30, 0}} };
                const FSeat& Seat = Seats[SpawnCount % 6];
                FFGBanditSpec Spec;
                Spec.Kind = SpawnCount % 4 == 2 ? EFGBanditKind::Dynamiter : EFGBanditKind::TrainShooter;
                Spec.Mesh = Spec.Kind == EFGBanditKind::Dynamiter ? TEXT("SK_Dynamiter") : TEXT("SK_Rifleman");
                Spec.Slot = Seat.Local + FVector(0, 0, AFGTrain::RoofCm);
                Spec.Anchor = OnCar(Seat.Car);
                Spec.FirstShotDelay = FMath::FRandRange(1.2f, 2.5f) * FireDelayScale();
                SpawnBandit(Spec);
                ++CrewSpawned;
            }
        }
        if (bTimeUp)
        {
            if (StageFlags == 0) { StageFlags = 1; EveryoneLeave(); }
            // It loses ground slowly, a few metres a second, and slips out of sight behind us. It used to shoot backwards
            // at 180 m/s, which read as vanishing. The duel starts once it is behind the camera; it is hidden later.
            BanditTrain->Offset = FMath::FInterpConstantTo(BanditTrain->Offset, -200.0, DeltaTime, 7.0f + TrainSpeed * 0.12f);
            if (BanditTrain->IsHidden() || BanditTrain->Offset < -45.0)
            {
                for (AFGTarget* T : Targets) { if (T && T->bExplosive && T->Local.Y == 0.0f) { T->Destroy(); } }      // the ones on the other train
                SetPhase(EFGPhase::Showdown);
            }
        }
        break;
    }
    }
}

void AFGIronHorseGameMode::TickShowdown(float DeltaTime)
{
    if (!BanditTrain->IsHidden())
    {
        BanditTrain->Offset = FMath::FInterpConstantTo(BanditTrain->Offset, -200.0, DeltaTime, 7.0f + TrainSpeed * 0.12f);
        if (BanditTrain->Offset < -110.0) { BanditTrain->SetActorHiddenInGame(true); }
    }
    ShowdownTimer -= DeltaTime;
    const FFGTrackerState& In = Player->Tracker->State;
    switch (ShowdownStep)
    {
    case 0:     // quiet, then he lands. Not until everything already laid ahead (450 m) is plain open track.
        if (ShowdownTimer <= 0.0f && !World->EventsWithin(500.0f))
        {
            FFGBanditSpec Spec;
            Spec.Kind = EFGBanditKind::Boss;
            Spec.Mesh = TEXT("SK_Boss");
            Spec.Slot = FVector(-90.0f, 0.0f, AFGTrain::RoofCm);
            Spec.Anchor = OnOwnCar(2);
            Boss = SpawnBandit(Spec);
            ShowdownStep = 1;
        }
        break;
    case 1:     // waiting for OnBossLanded
        break;
    case 2:     // holster
        Prompt = TEXT("HOLSTER");
        SubPrompt = Player->Tracker->bTrackerLive ? TEXT("drop your gun hand to your hip") : TEXT("hold H");
        HolsteredFor = In.bHolstered ? HolsteredFor + DeltaTime : 0.0f;
        HolsterWait += DeltaTime;
        // A booth player who never finds the holster pose still gets their duel.
        if (HolsteredFor > 0.6f || HolsterWait > 6.0f)
        {
            Prompt = TEXT("WAIT FOR IT...");
            SubPrompt = TEXT("");
            ShowdownTimer = FMath::FRandRange(1.8f, 3.4f);
            ShowdownStep = 3;
            PlaySfx(TEXT("heartbeat"));
        }
        break;
    case 3:     // the wait. Only a SHOT before the whistle is too early (see ResolvePlayerShot). This used to watch the
                // holster flag as well, and that flag flickers whenever the body model loses the arm for a frame:
                // players were told TOO EARLY while standing perfectly still.
        if (ShowdownTimer <= 0.0f)
        {
            Prompt = TEXT("DRAW!");
            PlaySfx(TEXT("whistle"));
            DrawCalledAt = FPlatformTime::Seconds();
            if (Boss) { Boss->Draw(FMath::Max(0.6f, 1.15f - 0.15f * Lap)); }
            ShowdownStep = 4;
        }
        break;
    case 4:     // live. Resolved by OnBanditKilled, or by his bullet.
        if (PhaseTime > 0.0f && FPlatformTime::Seconds() - DrawCalledAt > 2.0) { Prompt = TEXT(""); }
        if (Boss && !Boss->IsDead() && FPlatformTime::Seconds() - DrawCalledAt > 3.0 && ShowdownTimer <= 0.0f)
        {
            ShowdownTimer = 2.2f;
            Boss->Draw(0.7f);           // he keeps shooting until someone drops
        }
        break;
    case 5:     // after a shot too early: straight back to the wait, no need to holster again
        if (ShowdownTimer <= 0.0f)
        {
            Prompt = TEXT("WAIT FOR IT...");
            ShowdownTimer = FMath::FRandRange(1.8f, 3.2f);
            ShowdownStep = 3;
        }
        break;
    case 6:     // aftermath
        if (ShowdownTimer <= 0.0f) { BeginLap(Lap + 1); }        // no ending: it goes round again, harder, until you drop
        break;
    }
}

void AFGIronHorseGameMode::OnBossLanded()
{
    ShowdownStep = 2;
    HolsteredFor = 0.0f;
    HolsterWait = 0.0f;
    PlaySfx(TEXT("thud"));
}

// ------------------------------------------------------------------ shooting

bool AFGIronHorseGameMode::PlayerMayFire() const
{
    return Phase != EFGPhase::Title && !(Phase == EFGPhase::Result && ResultTime < 1.5f);
}

bool AFGIronHorseGameMode::HandleUiShot(FVector2D Aim)
{
    if (Phase != EFGPhase::Result) { return false; }
    PlaySfx(TEXT("shot_player"));
    // Any shot restarts, not only one on the button: a dead player has no steady crosshair, and the booth must never
    // need a keyboard.
    UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this)));
    return true;
}

void AFGIronHorseGameMode::OnPlayerDryFire() {}
void AFGIronHorseGameMode::OnPlayerReloaded() {}

bool AFGIronHorseGameMode::ResolvePlayerShot(const FVector& Origin, const FVector& Dir, const FVector& Muzzle)
{
    const FFGWeapon& Gun = Player->Weapon();
    PlaySfx(TEXT("shot_player"), 1.0f, Gun.SfxPitch);
    UWorld* W = GetWorld();

    // Webcam aim is noisy, so be generous: anything within a few degrees of the ray counts. Nearest to the ray wins.
    const float Assist = (Phase == EFGPhase::Bell ? 6.0f : AimAssistDegrees) * Gun.AssistScale;
    TArray<TPair<float, AFGBandit*>> AlsoHit;       // a shotgun's spread, a rifle going through
    AFGBandit* BestBandit = nullptr;
    AFGTarget* BestTarget = nullptr;
    float BestScore = 1.0f;
    FVector HitPoint = Origin + Dir * 6000.0f;
    bool bHead = false;

    const bool bShowdownLocked = Phase == EFGPhase::Showdown && ShowdownStep != 4;     // he can only be shot once DRAW is called
    for (AFGBandit* B : Bandits)
    {
        if (!B || B->IsDead() || (B == Boss && bShowdownLocked)) { continue; }
        FVector Chest, Head;
        B->AimPoints(Chest, Head);
        const float Dist = FVector::Dist(Origin, Chest);
        const float Allowed = FMath::Max(Assist, FMath::RadiansToDegrees(FMath::Atan(48.0f / Dist)));
        const float ChestScore = AngleBetween(Dir, Chest - Origin) / Allowed;
        const float HeadAngle = AngleBetween(Dir, Head - Origin);
        const bool bHeadHit = HeadAngle < FMath::RadiansToDegrees(FMath::Atan(27.0f / Dist)) + 0.6f;       // head and hat
        const float S = bHeadHit ? FMath::Min(ChestScore, 0.2f) : ChestScore;
        if (S < 1.0f && B != Boss) { AlsoHit.Emplace(S, B); }
        if (S < BestScore) { BestScore = S; BestBandit = B; BestTarget = nullptr; bHead = bHeadHit; HitPoint = bHeadHit ? Head : Chest; }
    }
    for (AFGTarget* T : Targets)
    {
        if (!T || !T->bActive) { continue; }
        const FVector C = T->Centre();
        const float Dist = FVector::Dist(Origin, C);
        const float Allowed = FMath::Max(Assist, FMath::RadiansToDegrees(FMath::Atan(T->Radius / Dist)));
        float S = AngleBetween(Dir, C - Origin) / Allowed;
        if (Phase == EFGPhase::Calibrate) { S = 0.0f; }     // any shot hits the bottle: that is what calibrates the tracker
        if (S < BestScore) { BestScore = S; BestTarget = T; BestBandit = nullptr; HitPoint = C; }
    }

    if (Phase == EFGPhase::Showdown && ShowdownStep == 3)
    {
        Prompt = TEXT("TOO EARLY");
        ShowdownTimer = 1.2f;
        ShowdownStep = 5;
    }

    const bool bHit = BestBandit || BestTarget;
    if (!bHit)
    {
        // Misses kick up dust where the ray meets the ground. (The chunks carry no collision: moving a dozen
        // triangle-mesh bodies every frame just for this was wasted physics time.)
        if (Dir.Z < -0.01f)
        {
            const float Dist = -Origin.Z / Dir.Z;
            if (Dist < 30000.0f)
            {
                HitPoint = Origin + Dir * Dist;
                if (AFGFx* P = AFGFx::Spawn(W, TEXT("fx"), TEXT("SM_Puff_Dust"), FTransform(FRotator::ZeroRotator, HitPoint, FVector(0.5f)), 0.7f, FVector(-TrainSpeed * 100.0f, 0, 120.0f), 3.0f)) { P->Glow(FLinearColor(0.80f, 0.62f, 0.40f), 0.5f); }
            }
        }
    }

    // Flash, tracer
    const FVector ToHit = (HitPoint - Muzzle).GetSafeNormal();
    const FQuat Along = ToHit.ToOrientationQuat() * FQuat(FRotator(0.0, -90.0, 0.0));     // fx meshes point along +Y
    if (AFGFx* Flash = AFGFx::Spawn(W, TEXT("fx"), Gun.Tracers > 1 ? TEXT("SM_MuzzleFlash_Big") : TEXT("SM_MuzzleFlash_A"), FTransform(Along, Muzzle, FVector(0.6f)), 0.06f))
    {
        Flash->AddLight(FLinearColor(1.0f, 0.7f, 0.35f), 900.0f, 1500.0f);
    }
    const float TracerSpeed = 40000.0f;
    for (int32 i = 1; i < Gun.Tracers; ++i)
    {
        // pellets: the same tracer, fanned out
        const FVector Spread = (ToHit + FVector(FMath::FRandRange(-0.06f, 0.06f), FMath::FRandRange(-0.06f, 0.06f), FMath::FRandRange(-0.04f, 0.04f))).GetSafeNormal();
        AFGFx::Spawn(W, TEXT("fx"), TEXT("SM_Tracer_Player"), FTransform(Spread.ToOrientationQuat() * FQuat(FRotator(0.0, -90.0, 0.0)), Muzzle, FVector(1.0f, 1.2f, 1.0f)), 0.09f, Spread * TracerSpeed);
    }
    AFGFx::Spawn(W, TEXT("fx"), TEXT("SM_Tracer_Player"), FTransform(Along, Muzzle, FVector(1.0f, 2.0f, 1.0f)), FMath::Clamp(FVector::Dist(Muzzle, HitPoint) / TracerSpeed, 0.03f, 0.2f), ToHit * TracerSpeed);

    if (BestBandit)
    {
        BestBandit->bHeadshot = bHead;
        FHitResult Fake;
        Fake.ImpactPoint = Fake.Location = HitPoint;
        UGameplayStatics::ApplyPointDamage(BestBandit, bHead ? 100.0f : Player->ShotDamage, Dir, Fake, Player->GetController(), Player, nullptr);
        if (!BestBandit->IsDead()) { BestBandit->Play(TEXT("hit")); PlaySfx(TEXT("grunt")); }
        AlsoHit.Sort([](const TPair<float, AFGBandit*>& A, const TPair<float, AFGBandit*>& B) { return A.Key < B.Key; });
        int32 Extra = Gun.MaxHits - 1;
        for (const TPair<float, AFGBandit*>& Other : AlsoHit)
        {
            if (Extra <= 0) { break; }
            if (Other.Value == BestBandit || Other.Value->IsDead()) { continue; }
            FVector Chest, Head;
            Other.Value->AimPoints(Chest, Head);
            Fake.ImpactPoint = Fake.Location = Chest;
            Other.Value->bHeadshot = false;
            UGameplayStatics::ApplyPointDamage(Other.Value, Player->ShotDamage, Dir, Fake, Player->GetController(), Player, nullptr);
            --Extra;
        }
        if (AFGFx* P = AFGFx::Spawn(W, TEXT("fx"), TEXT("SM_Puff_Dust"), FTransform(FRotator::ZeroRotator, HitPoint, FVector(0.25f)), 0.35f, FVector::ZeroVector, 4.0f)) { P->Glow(FLinearColor(0.80f, 0.62f, 0.40f), 0.5f); }
        HitMarker = 1.0f;
    }
    else if (BestTarget)
    {
        BestTarget->Shot();
        HitMarker = 1.0f;
    }
    return bHit;
}

void AFGIronHorseGameMode::ShootablePoints(TArray<FVector>& Out) const
{
    const bool bBossLocked = Phase == EFGPhase::Showdown && ShowdownStep != 4;
    for (const AFGBandit* B : Bandits)
    {
        if (!B || B->IsDead() || (B == Boss && bBossLocked)) { continue; }
        FVector Chest, Head;
        B->AimPoints(Chest, Head);
        Out.Add(Chest);
    }
    for (const AFGTarget* T : Targets)
    {
        if (T && T->bActive) { Out.Add(T->Centre()); }
    }
}

FVector2D AFGIronHorseGameMode::Magnet(FVector2D Raw, FVector2D& Offset, const TArray<FVector>& Points, float RealDelta) const
{
    FVector2D Want = FVector2D::ZeroVector;
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    int32 W = 0, H = 0;
    if (PC) { PC->GetViewportSize(W, H); }
    if (PC && W > 0)
    {
        constexpr float Reach = 0.13f;          // screen heights
        const float Aspect = float(W) / float(H);
        float BestD = Reach;
        for (const FVector& P : Points)
        {
            FVector2D Px;
            if (!PC->ProjectWorldLocationToScreen(P, Px)) { continue; }
            const FVector2D N(Px.X / W, Px.Y / H);
            const float D = FMath::Sqrt(FMath::Square((N.X - Raw.X) * Aspect) + FMath::Square(N.Y - Raw.Y));
            if (D < BestD)
            {
                BestD = D;
                Want = (N - Raw) * (0.75f * FMath::Pow(1.0f - D / Reach, 0.6f));
            }
        }
    }
    Offset = FMath::Vector2DInterpTo(Offset, Want, RealDelta, 12.0f);
    return Raw + Offset;
}

void AFGIronHorseGameMode::TickMagnet(float DeltaTime)
{
    // Aim magnetism: near a target the crosshair leans onto it, harder the closer it gets. It hides the last of the
    // hand jitter exactly where it matters and makes a webcam feel like it is aiming for you, the way console shooters do.
    const FFGTrackerState& In = Player->Tracker->State;
    TArray<FVector> Points;
    if (Player->Tracker->bTrackerLive && Phase != EFGPhase::Result && Phase != EFGPhase::Title)
    {
        ShootablePoints(Points);
    }
    const float RealDelta = FApp::GetDeltaTime();
    AssistedAim = Magnet(FVector2D(In.AimX, In.AimY), MagnetOffset, Points, RealDelta);
    AssistedAim2 = Magnet(FVector2D(In.Aim2X, In.Aim2Y), MagnetOffset2, Points, RealDelta);
}

bool AFGIronHorseGameMode::PlayerCanSee(const FVector& WorldPoint) const
{
    const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    int32 W = 0, H = 0;
    FVector2D Screen;
    if (!PC) { return false; }
    PC->GetViewportSize(W, H);
    if (W <= 0 || !PC->ProjectWorldLocationToScreen(WorldPoint, Screen)) { return false; }
    return Screen.X > W * 0.04f && Screen.X < W * 0.96f && Screen.Y > H * 0.06f && Screen.Y < H * 0.94f;
}

bool AFGIronHorseGameMode::RequestAttackToken()
{
    if (TokensOut >= TokenLimit() || Phase == EFGPhase::Result || bPlayerDead) { return false; }
    ++TokensOut;
    return true;
}

void AFGIronHorseGameMode::ReleaseAttackToken()
{
    TokensOut = FMath::Max(0, TokensOut - 1);
}

void AFGIronHorseGameMode::OnTelegraph(AFGBandit* Bandit)
{
    // The bandit flashes red at the barrel for the 0.7 s (AFGBandit::Warning). Here: the rising tone.
    PlaySfx(TEXT("telegraph"), 0.8f);
}

void AFGIronHorseGameMode::OnBanditKilled(AFGBandit* Bandit)
{
    ++Kills;
    Score += 100;
    if (Bandit->bHeadshot)
    {
        ++Headshots;
        Score += 50;
        if (Player->Hats < 3)
        {
            ++Player->Hats;         // a headshot wins a hat back
            Banner = TEXT("+1 HAT");
            BannerTime = 1.2f;
            PlaySfx(TEXT("bell"), 0.6f, 1.6f);
        }
    }
    PlaySfx(TEXT("grunt"));
    if (Bandit == Boss)
    {
        bBossBeaten = true;
        ++BossesBeaten;
        Score += 500;
        if (ShowdownStep == 4)
        {
            const float Ms = float((FPlatformTime::Seconds() - DrawCalledAt) * 1000.0);
            DrawTimeMs = DrawTimeMs < 0.0f ? Ms : FMath::Min(DrawTimeMs, Ms);      // best draw of the run
            Score += FMath::Max(0, 1000 - int32(Ms / 2.0f));
        }
        Prompt = TEXT("");
        ShowdownStep = 6;
        ShowdownTimer = 3.0f;
    }
}

void AFGIronHorseGameMode::SpawnEnemyShot(const FVector& From, bool bFast)
{
    // Aimed at where the head is now. It takes 0.7 s to arrive: move and it misses.
    FFGEnemyShot& Shot = Shots.AddDefaulted_GetRef();
    Shot.bAccurate = bFast || FMath::FRand() < 0.6f;
    Shot.Target = Player->HeadLocation();
    if (!Shot.bAccurate)
    {
        const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
        Shot.Target += FVector(0.0f, FMath::Cos(Angle), FMath::Sin(Angle) * 0.5f + 0.5f) * FMath::FRandRange(60.0f, 110.0f);
    }
    Shot.TimeLeft = bFast ? 0.3f : ShotFlight();
    const FVector Dir = (Shot.Target - From).GetSafeNormal();
    const float Speed = FVector::Dist(From, Shot.Target) / Shot.TimeLeft;
    const FQuat Along = Dir.ToOrientationQuat() * FQuat(FRotator(0.0, -90.0, 0.0));
    AFGFx* Fx = AFGFx::Spawn(GetWorld(), TEXT("fx"), TEXT("SM_Tracer_Enemy"), FTransform(Along, From, FVector(1.4f)), Shot.TimeLeft + 0.35f, Dir * Speed);
    if (Fx) { Fx->AddLight(FLinearColor(1.0f, 0.35f, 0.15f), 400.0f, 900.0f); }
    Shot.Fx = Fx;
    if (AFGFx* Flash = AFGFx::Spawn(GetWorld(), TEXT("fx"), TEXT("SM_MuzzleFlash_Big"), FTransform(Along, From, FVector(0.8f)), 0.07f))
    {
        Flash->AddLight(FLinearColor(1.0f, 0.6f, 0.3f), 1200.0f, 2500.0f);
    }
    PlaySfx(TEXT("shot_enemy"), 0.9f);
}

void AFGIronHorseGameMode::SpawnDynamite(const FVector& From)
{
    // Lobbed at the player. Shoot it in the air, or lean out of the blast.
    const float Flight = 2.0f;
    const FVector Target = Player->HeadLocation() - FVector(0, 0, 60.0f);
    const float G = 980.0f;
    FVector V = (Target - From) / Flight;
    V.Z += 0.5f * G * Flight;
    AFGTarget* Stick = SpawnTarget(TEXT("props"), TEXT("SM_Dynamite"), FTransform(FRotator::ZeroRotator, From, FVector(2.6f)), 55.0f);
    Stick->Velocity = V;
    Stick->Gravity = G;
    Stick->Fuse = Flight;
    Stick->OnShot = [this](AFGTarget* T)
    {
        Score += 150;
        PlaySfx(TEXT("boom"));
        if (AFGFx* Boom = AFGFx::Spawn(GetWorld(), TEXT("fx"), TEXT("SM_MuzzleFlash_Big"), FTransform(FRotator::ZeroRotator, T->GetActorLocation(), FVector(3.0f)), 0.3f, FVector::ZeroVector, 6.0f))
        {
            Boom->Glow(FLinearColor(3.0f, 1.4f, 0.4f), 0.4f);
            Boom->AddLight(FLinearColor(1.0f, 0.6f, 0.25f), 6000.0f, 5000.0f);
        }
        if (AFGFx* P = AFGFx::Spawn(GetWorld(), TEXT("fx"), TEXT("SM_Puff_Smoke"), FTransform(FRotator::ZeroRotator, T->GetActorLocation(), FVector(1.0f)), 1.4f, FVector(-TrainSpeed * 100.0f, 0, 100.0f), 2.5f)) { P->Glow(FLinearColor(0.12f, 0.11f, 0.10f), 0.5f); }
    };
    Stick->OnFuse = [this, Target](AFGTarget* T)
    {
        PlaySfx(TEXT("boom"));
        if (AFGFx* Boom = AFGFx::Spawn(GetWorld(), TEXT("fx"), TEXT("SM_MuzzleFlash_Big"), FTransform(FRotator::ZeroRotator, T->GetActorLocation(), FVector(4.0f)), 0.3f, FVector::ZeroVector, 6.0f))
        {
            Boom->Glow(FLinearColor(3.0f, 1.2f, 0.3f), 0.4f);
            Boom->AddLight(FLinearColor(1.0f, 0.6f, 0.25f), 8000.0f, 6000.0f);
        }
        const FVector Head = Player->HeadLocation();
        if (FMath::Abs(Head.Y - Target.Y) < 70.0f) { HurtPlayer(TEXT("hurt")); }
        else { ++Dodges; Score += 25; }
    };
}

void AFGIronHorseGameMode::TickShots(float DeltaTime)
{
    for (int32 i = Shots.Num() - 1; i >= 0; --i)
    {
        FFGEnemyShot& Shot = Shots[i];
        Shot.TimeLeft -= DeltaTime;
        if (Shot.TimeLeft > 0.0f) { continue; }
        const float Miss = FVector::Dist(Player->HeadLocation(), Shot.Target);
        if (Shot.bAccurate && Miss < Shot.Radius)
        {
            HurtPlayer(TEXT("hurt"));
        }
        else
        {
            if (Shot.bAccurate) { ++Dodges; Score += 25; }
            PlaySfx(TEXT("whiz"), 0.9f, FMath::FRandRange(0.9f, 1.15f));
        }
        Shots.RemoveAtSwap(i);
    }
}

void AFGIronHorseGameMode::HurtPlayer(const TCHAR* Sfx)
{
    if (bPlayerDead || bGod || InvulnerableFor > 0.0f || Phase == EFGPhase::Result) { return; }
    InvulnerableFor = 1.5f;
    PlaySfx(Sfx);
    Player->Tracker->SendHit();
    const int32 Before = Player->Hats;
    const bool bDead = Player->TakeHit();
    if (Before == 3)
    {
        // First hit shoots your hat off. You see it go.
        AFGFx::Spawn(GetWorld(), TEXT("props"), TEXT("SM_Hat_Deputy"), FTransform(FRotator(0, 90, 0), Player->HeadLocation() + FVector(60, 0, 25)), 3.0f,
            FVector(900.0f, FMath::FRandRange(-200.f, 200.f), 350.0f), 0.0f, 600.0f, FRotator(300.0f, 200.0f, 0.0f));
    }
    if (bDead)
    {
        bPlayerDead = true;
        Player->bGunHidden = true;
        SetPhase(EFGPhase::Result);
    }
}

void AFGIronHorseGameMode::TickDuck()
{
    // Low things over the track: water tower spout, signal gantry, tunnel mouths.
    const float D = World->MetresTo(TEXT("duck"));
    bDuckWarning = D > 0.0f && D < FMath::Max(TrainSpeed, 8.0f) * 2.6f && TrainSpeed > 3.0f;
    // The event is measured at the player's car, the player stands a little ahead of its centre.
    const float Ahead = PlayerForwardCm / 100.0f;
    const bool bPassed = LastDuckDistance > Ahead && (D < 0.0f || D <= Ahead || D > LastDuckDistance + 5.0f);
    if (bPassed && TrainSpeed > 3.0f)
    {
        if (Player->Tracker->State.Duck < 0.45f) { HurtPlayer(TEXT("bonk")); }
        else { ++Dodges; Score += 50; PlaySfx(TEXT("whiz"), 1.0f, 0.6f); }
    }
    LastDuckDistance = D;
}

void AFGIronHorseGameMode::TickLeanObstacles(float DeltaTime)
{
    // Signal arms over one half of the roof: lean to the other side. Every 9-16 s of open running, never near a
    // tunnel, bridge, canyon or another duck, and never in a duel.
    float Side = 0.0f;
    const float D = World->MetresToLeanObstacle(Side);
    const bool bRiding = Phase == EFGPhase::Ride && TrainSpeed > 10.0f;
    ObstacleTimer -= bRiding ? DeltaTime : 0.0f;
    const float Ahead = FMath::Max(TrainSpeed, 15.0f) * 4.5f;
    if (bRiding && ObstacleTimer <= 0.0f && D < 0.0f && !World->EventsWithin(Ahead + 120.0f))
    {
        ObstacleTimer = FMath::FRandRange(9.0f, 16.0f) * FMath::Max(0.6f, 1.0f - 0.1f * Lap);
        World->AddLeanObstacle(Ahead, FMath::RandBool() ? 1.0f : -1.0f);
    }
    // The arm is over the Side half, so the warning points the other way.
    LeanWarning = D > 0.0f && D < FMath::Max(TrainSpeed, 8.0f) * 2.8f ? -Side : 0.0f;
    const float Here = PlayerForwardCm / 100.0f;
    if (LastLeanDistance > Here && D >= 0.0f && D <= Here)
    {
        // The arm covers from the centreline outwards on its side, at standing and ducked head height alike.
        const float HeadY = Player->HeadLocation().Y * Side;
        if (HeadY > -30.0f) { HurtPlayer(TEXT("bonk")); }
        else { ++Dodges; Score += 50; PlaySfx(TEXT("whiz"), 1.0f, 0.6f); }
    }
    LastLeanDistance = D;
}

void AFGIronHorseGameMode::TickAtmosphere(float DeltaTime)
{
    // Under a tunnel roof (5.9 m, the standing eye is at 6.0): the view is held below it so it cannot poke through,
    // and anyone not actually ducking is scraping along the ceiling. That costs a hat every second and a half.
    bStayDown = World->MetresTo(TEXT("dark")) == 0.0f && TrainSpeed > 3.0f;
    Player->ForcedDropCm = bStayDown ? 45.0f : 0.0f;
    if (bStayDown && Player->Tracker->State.Duck < 0.45f && PhaseTime > 0.0f) { HurtPlayer(TEXT("bonk")); }

    const bool bDark = World->MetresTo(TEXT("dark")) == 0.0f;
    Darkness = FMath::FInterpTo(Darkness, bDark ? 1.0f : 0.0f, DeltaTime, 2.5f);
    if (!bOwnSky)
    {
        if (Sun) { Sun->GetLightComponent()->SetIntensity(SunIntensity * (1.0f - 0.97f * Darkness)); }
        if (Sky) { Sky->GetLightComponent()->SetIntensity(1.3f * (1.0f - 0.9f * Darkness)); }
        if (Lantern) { Lantern->SetIntensity(Darkness * 320.0f); }
        return;
    }
    // The day runs with the lap: golden hour at the station, the sun sinking dead ahead so the boss stands in front
    // of it, then night after he falls, and morning after the next one.
    if (Phase == EFGPhase::Ride && NightTarget < 0.5f) { Dusk = FMath::Max(Dusk, FMath::Clamp((int32(Stage) * StageSeconds + StageTime) / (3.4f * StageSeconds), 0.0f, 0.9f)); }
    if (Phase == EFGPhase::Showdown && NightTarget < 0.5f) { Dusk = FMath::FInterpConstantTo(Dusk, 1.0f, DeltaTime, 0.08f); }
    Night = FMath::FInterpConstantTo(Night, NightTarget, DeltaTime, 0.12f);

    const float Pitch = FMath::Lerp(FMath::Lerp(-16.0f, -2.5f, Dusk), -38.0f, Night);
    // The sun belongs to the landscape: on a curve it swings round with the mesas, it does not ride along with the
    // camera. For the duel it is steered, slowly, to stand dead ahead in the landscape as it is then (the line is
    // kept straight meanwhile), so the boss has it behind him.
    if (Phase == EFGPhase::Showdown && NightTarget < 0.5f)
    {
        const float Want = 180.0f - World->WorldYaw();
        SunChainYaw += FMath::Clamp(FMath::FindDeltaAngleDegrees(SunChainYaw, Want), -14.0f * DeltaTime, 14.0f * DeltaTime);
    }
    const float Yaw = SunChainYaw + World->WorldYaw() + 25.0f * Night;
    const FLinearColor DayColor = FMath::Lerp(FLinearColor(1.0f, 0.80f, 0.58f), FLinearColor(1.0f, 0.42f, 0.18f), Dusk);
    const FLinearColor Color = FMath::Lerp(DayColor, FLinearColor(0.45f, 0.58f, 1.0f), Night);
    const float Intensity = FMath::Lerp(FMath::Lerp(SunIntensity, SunIntensity * 0.6f, Dusk), 0.7f, Night);
    UDirectionalLightComponent* SunComp = Cast<UDirectionalLightComponent>(Sun->GetLightComponent());
    Sun->SetActorRotation(FRotator(Pitch, Yaw, 0.0f));
    SunComp->SetLightColor(Color);
    SunComp->SetIntensity(Intensity * (1.0f - 0.97f * Darkness));
    const bool bSunInSky = Night < 0.5f;       // at night the same light is the moon, and must not light the atmosphere up like a sun
    if (SunComp->IsUsedAsAtmosphereSunLight() != bSunInSky) { SunComp->SetAtmosphereSunLight(bSunInSky); }
    Sky->GetLightComponent()->SetIntensity(FMath::Lerp(1.3f, 0.5f, Night) * (1.0f - 0.9f * Darkness));
    if (Fog)
    {
        Fog->GetComponent()->SetFogInscatteringColor(FMath::Lerp(FMath::Lerp(FLinearColor(0.85f, 0.55f, 0.32f), FLinearColor(0.9f, 0.35f, 0.15f), Dusk), FLinearColor(0.02f, 0.035f, 0.08f), Night));
    }
    Lantern->SetIntensity(FMath::Max(Darkness * 320.0f, Night * 140.0f));
    // The sky light is a one-off capture (cheap). Take a new one whenever the sky has visibly moved on.
    const float Key = Dusk * 6.0f + Night * 8.0f;
    if (FMath::Abs(Key - SkyKey) > 1.0f && GetWorld()->GetTimeSeconds() > 1.0f)
    {
        SkyKey = Key;
        Sky->GetLightComponent()->RecaptureSky();
    }
}

void AFGIronHorseGameMode::PlaySfx(const FString& Name, float Volume, float Pitch)
{
    if (USoundBase* Sound = FGAssets::Sound(Name))
    {
        UGameplayStatics::PlaySound2D(this, Sound, Volume, Pitch);
    }
}

FString AFGIronHorseGameMode::Rank() const
{
    if (Score >= 9000) { return TEXT("LEGEND"); }
    if (Score >= 5500) { return TEXT("BOUNTY HUNTER"); }
    if (Score >= 3200) { return TEXT("GUNSLINGER"); }
    if (Score >= 1900) { return TEXT("DEPUTY"); }
    if (Score >= 900) { return TEXT("DRIFTER"); }
    return TEXT("GREENHORN");
}

float AFGIronHorseGameMode::Accuracy() const
{
    return Player && Player->ShotsFired > 0 ? float(Player->ShotsHit) / float(Player->ShotsFired) : 0.0f;
}
