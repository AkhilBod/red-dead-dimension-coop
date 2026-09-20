#include "FingerGunPlayerCharacter.h"

#include "Blueprint/UserWidget.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"


AFingerGunPlayerCharacter::AFingerGunPlayerCharacter()
{
    PrimaryActorTick.bCanEverTick = true;


    // ============================================================
    // CAMERA
    // ============================================================

    FirstPersonCamera =
        CreateDefaultSubobject<UCameraComponent>(
            TEXT("FirstPersonCamera")
        );

    FirstPersonCamera->SetupAttachment(
        GetCapsuleComponent()
    );

    /*
     * Aim is independent from camera rotation.
     *
     * The camera represents where the player's body/head is.
     * The crosshair is controlled separately.
     */
    FirstPersonCamera->bUsePawnControlRotation = false;

    FirstPersonCamera->SetRelativeLocation(
        FVector(
            0.0f,
            0.0f,
            StandingCameraZ
        )
    );


    // ============================================================
    // NORMAL CHARACTER MOVEMENT DISABLED
    // ============================================================

    bUseControllerRotationYaw = false;
    bUseControllerRotationPitch = false;
    bUseControllerRotationRoll = false;


    if (UCharacterMovementComponent* Movement =
        GetCharacterMovement())
    {
        Movement->GravityScale = 0.0f;

        Movement->MaxWalkSpeed = 0.0f;

        Movement->bOrientRotationToMovement = false;
    }
}


void AFingerGunPlayerCharacter::BeginPlay()
{
    Super::BeginPlay();


    // ============================================================
    // SAVE STARTING BODY STATE
    // ============================================================

    StartingActorLocation =
        GetActorLocation();

    StartingRightVector =
        GetActorRightVector();

    StartingUpVector =
        GetActorUpVector();


    if (FirstPersonCamera)
    {
        StartingCameraRelativeLocation =
            FirstPersonCamera->GetRelativeLocation();

        StandingCameraZ =
            StartingCameraRelativeLocation.Z;
    }


    if (UCapsuleComponent* Capsule =
        GetCapsuleComponent())
    {
        StandingCapsuleHalfHeight =
            Capsule->GetUnscaledCapsuleHalfHeight();
    }


    // ============================================================
    // DISABLE CHARACTER MOVEMENT
    // ============================================================

    if (UCharacterMovementComponent* Movement =
        GetCharacterMovement())
    {
        Movement->StopMovementImmediately();

        Movement->DisableMovement();

        Movement->GravityScale = 0.0f;
    }


    // ============================================================
    // INITIAL BODY STATE
    // ============================================================

    Lean = 0.0f;

    Height = 1.0f;

    UpdateBodyTransform();


    // ============================================================
    // INITIAL AIM STATE
    // ============================================================

    AimX = 0.0f;

    AimY = 0.0f;


    // ============================================================
    // INITIAL WEAPON STATE
    // ============================================================

    CurrentAmmo =
        MagazineSize;


    // ============================================================
    // CROSSHAIR
    // ============================================================

    CreateCrosshairWidget();

    UpdateCrosshairPosition();
}


void AFingerGunPlayerCharacter::Tick(
    float DeltaTime)
{
    Super::Tick(DeltaTime);

    /*
     * We update every frame because CV may eventually change aim
     * constantly, and the viewport size may also change.
     */
    UpdateCrosshairPosition();
}


// ================================================================
// BODY INPUT
// ================================================================

void AFingerGunPlayerCharacter::SetLean(
    float NewLean)
{
    Lean =
        FMath::Clamp(
            NewLean,
            -1.0f,
            1.0f
        );

    UpdateBodyTransform();
}


void AFingerGunPlayerCharacter::SetHeight(
    float NewHeight)
{
    Height =
        FMath::Clamp(
            NewHeight,
            0.0f,
            1.0f
        );

    UpdateBodyTransform();
}


void AFingerGunPlayerCharacter::SetBodyInput(
    float NewLean,
    float NewHeight)
{
    Lean =
        FMath::Clamp(
            NewLean,
            -1.0f,
            1.0f
        );

    Height =
        FMath::Clamp(
            NewHeight,
            0.0f,
            1.0f
        );

    UpdateBodyTransform();
}


void AFingerGunPlayerCharacter::ResetBodyInput()
{
    Lean = 0.0f;

    Height = 1.0f;

    UpdateBodyTransform();
}


float AFingerGunPlayerCharacter::GetLean() const
{
    return Lean;
}


float AFingerGunPlayerCharacter::GetHeight() const
{
    return Height;
}


// ================================================================
// BODY TRANSFORM
// ================================================================

void AFingerGunPlayerCharacter::UpdateBodyTransform()
{
    // ============================================================
    // LEAN
    // ============================================================

    const float SideOffset =
        Lean * LeanDistance;


    // ============================================================
    // CROUCH CAPSULE
    // ============================================================

    const float CurrentCapsuleHalfHeight =
        FMath::Lerp(
            CrouchedCapsuleHalfHeight,
            StandingCapsuleHalfHeight,
            Height
        );


    /*
     * Shrinking the capsule happens around its center.
     *
     * Move the Actor downward so the capsule bottom remains
     * approximately where it started.
     */
    const float VerticalAdjustment =
        StandingCapsuleHalfHeight
        -
        CurrentCapsuleHalfHeight;


    // ============================================================
    // ACTOR LOCATION
    // ============================================================

    const FVector LeanOffset =
        StartingRightVector
        *
        SideOffset;


    const FVector CrouchOffset =
        StartingUpVector
        *
        -VerticalAdjustment;


    const FVector NewActorLocation =
        StartingActorLocation
        +
        LeanOffset
        +
        CrouchOffset;


    SetActorLocation(
        NewActorLocation,
        false,
        nullptr,
        ETeleportType::TeleportPhysics
    );


    // ============================================================
    // CAPSULE HITBOX
    // ============================================================

    if (UCapsuleComponent* Capsule =
        GetCapsuleComponent())
    {
        Capsule->SetCapsuleHalfHeight(
            CurrentCapsuleHalfHeight,
            true
        );
    }


    // ============================================================
    // CAMERA HEIGHT
    // ============================================================

    if (FirstPersonCamera)
    {
        const float CurrentCameraZ =
            FMath::Lerp(
                CrouchedCameraZ,
                StandingCameraZ,
                Height
            );


        FVector CameraLocation =
            StartingCameraRelativeLocation;


        CameraLocation.Z =
            CurrentCameraZ;


        FirstPersonCamera->SetRelativeLocation(
            CameraLocation
        );
    }
}


// ================================================================
// CROSSHAIR CREATION
// ================================================================

void AFingerGunPlayerCharacter::CreateCrosshairWidget()
{
    if (!CrosshairWidgetClass)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT(
                "FingerGunPlayerCharacter: "
                "CrosshairWidgetClass is not assigned."
            )
        );

        return;
    }


    APlayerController* PlayerController =
        Cast<APlayerController>(
            GetController()
        );


    if (!PlayerController)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT(
                "FingerGunPlayerCharacter: "
                "No PlayerController available."
            )
        );

        return;
    }


    CrosshairWidget =
        CreateWidget<UUserWidget>(
            PlayerController,
            CrosshairWidgetClass
        );


    if (!CrosshairWidget)
    {
        return;
    }


    CrosshairWidget->AddToViewport();


    /*
     * Treat widget position as its center instead of
     * its upper-left corner.
     */
    CrosshairWidget->SetAlignmentInViewport(
        FVector2D(
            0.5f,
            0.5f
        )
    );
}


// ================================================================
// MOUSE / DEBUG AIM
// ================================================================

void AFingerGunPlayerCharacter::AddAimInput(
    FVector2D InputDelta)
{
    float VerticalInput =
        InputDelta.Y;


    if (bInvertAimY)
    {
        VerticalInput *= -1.0f;
    }


    AimX +=
        InputDelta.X
        *
        AimSensitivity;


    AimY +=
        VerticalInput
        *
        AimSensitivity;


    AimX =
        FMath::Clamp(
            AimX,
            -1.0f,
            1.0f
        );


    AimY =
        FMath::Clamp(
            AimY,
            -1.0f,
            1.0f
        );
}


// ================================================================
// CV AIM
// ================================================================

void AFingerGunPlayerCharacter::SetAimNormalized(
    float NewAimX,
    float NewAimY)
{
    AimX =
        FMath::Clamp(
            NewAimX,
            -1.0f,
            1.0f
        );


    AimY =
        FMath::Clamp(
            NewAimY,
            -1.0f,
            1.0f
        );
}


void AFingerGunPlayerCharacter::ResetAim()
{
    AimX = 0.0f;

    AimY = 0.0f;
}


FVector2D
AFingerGunPlayerCharacter::GetAimNormalized() const
{
    return FVector2D(
        AimX,
        AimY
    );
}


FVector2D
AFingerGunPlayerCharacter::GetCrosshairScreenPosition() const
{
    return CrosshairScreenPosition;
}


// ================================================================
// CROSSHAIR POSITION
// ================================================================

void AFingerGunPlayerCharacter::UpdateCrosshairPosition()
{
    APlayerController* PlayerController =
        Cast<APlayerController>(
            GetController()
        );


    if (!PlayerController)
    {
        return;
    }


    int32 ViewportWidth = 0;

    int32 ViewportHeight = 0;


    PlayerController->GetViewportSize(
        ViewportWidth,
        ViewportHeight
    );


    if (
        ViewportWidth <= 0
        ||
        ViewportHeight <= 0
        )
    {
        return;
    }


    const float Width =
        static_cast<float>(
            ViewportWidth
            );


    const float ViewHeight =
        static_cast<float>(
            ViewportHeight
            );


    // ------------------------------------------------------------
    // SCREEN BOUNDS
    // ------------------------------------------------------------

    const float MinX =
        CrosshairScreenMargin;

    const float MaxX =
        Width
        -
        CrosshairScreenMargin;


    const float MinY =
        CrosshairScreenMargin;

    const float MaxY =
        ViewHeight
        -
        CrosshairScreenMargin;


    // ------------------------------------------------------------
    // NORMALIZED [-1,1] -> [0,1]
    // ------------------------------------------------------------

    const float NormalizedX =
        (AimX + 1.0f)
        *
        0.5f;


    /*
     * Screen Y grows downward.
     *
     * AimY +1 should mean "top",
     * so Y has to be flipped.
     */
    const float NormalizedY =
        (1.0f - AimY)
        *
        0.5f;


    const float ScreenX =
        FMath::Lerp(
            MinX,
            MaxX,
            NormalizedX
        );


    const float ScreenY =
        FMath::Lerp(
            MinY,
            MaxY,
            NormalizedY
        );


    CrosshairScreenPosition =
        FVector2D(
            ScreenX,
            ScreenY
        );


    if (CrosshairWidget)
    {
        CrosshairWidget->SetPositionInViewport(
            CrosshairScreenPosition,
            true
        );
    }
}


// ================================================================
// CROSSHAIR -> WORLD RAY
// ================================================================

bool AFingerGunPlayerCharacter::GetCrosshairWorldRay(
    FVector& OutWorldOrigin,
    FVector& OutWorldDirection
) const
{
    APlayerController* PlayerController =
        Cast<APlayerController>(
            GetController()
        );


    if (!PlayerController)
    {
        return false;
    }


    const bool bSuccess =
        PlayerController
        ->DeprojectScreenPositionToWorld(
            CrosshairScreenPosition.X,
            CrosshairScreenPosition.Y,
            OutWorldOrigin,
            OutWorldDirection
        );


    if (!bSuccess)
    {
        return false;
    }


    OutWorldDirection =
        OutWorldDirection.GetSafeNormal();


    return true;
}


// ================================================================
// CAN FIRE
// ================================================================

bool AFingerGunPlayerCharacter::CanFire() const
{
    if (
        !bInfiniteAmmo
        &&
        CurrentAmmo <= 0
        )
    {
        return false;
    }


    if (!GetWorld())
    {
        return false;
    }


    const float CurrentTime =
        GetWorld()->GetTimeSeconds();


    const float TimeSinceLastShot =
        CurrentTime
        -
        LastFireTime;


    return
        TimeSinceLastShot
        >=
        FireCooldown;
}


// ================================================================
// FIRE HITSCAN
// ================================================================

void AFingerGunPlayerCharacter::Fire()
{
    // ------------------------------------------------------------
    // EMPTY MAGAZINE
    // ------------------------------------------------------------

    if (
        !bInfiniteAmmo
        &&
        CurrentAmmo <= 0
        )
    {
        OnDryFire();

        return;
    }


    // ------------------------------------------------------------
    // COOLDOWN
    // ------------------------------------------------------------

    if (!CanFire())
    {
        return;
    }


    // ------------------------------------------------------------
    // GET RAY FROM CROSSHAIR
    // ------------------------------------------------------------

    FVector WorldOrigin;

    FVector WorldDirection;


    if (
        !GetCrosshairWorldRay(
            WorldOrigin,
            WorldDirection
        )
        )
    {
        return;
    }


    // ------------------------------------------------------------
    // CONSUME SHOT
    // ------------------------------------------------------------

    LastFireTime =
        GetWorld()->GetTimeSeconds();


    if (!bInfiniteAmmo)
    {
        CurrentAmmo =
            FMath::Max(
                0,
                CurrentAmmo - 1
            );
    }


    // ------------------------------------------------------------
    // BUILD HITSCAN
    // ------------------------------------------------------------

    const FVector TraceStart =
        WorldOrigin;


    const FVector TraceEnd =
        TraceStart
        +
        WorldDirection
        *
        ShotRange;


    FCollisionQueryParams QueryParams;


    QueryParams.AddIgnoredActor(
        this
    );


    QueryParams.bTraceComplex =
        true;


    FHitResult HitResult;


    const bool bHit =
        GetWorld()->LineTraceSingleByChannel(
            HitResult,
            TraceStart,
            TraceEnd,
            ECC_Visibility,
            QueryParams
        );


    FVector FinalTraceEnd =
        TraceEnd;


    // ------------------------------------------------------------
    // HIT SOMETHING
    // ------------------------------------------------------------

    if (bHit)
    {
        FinalTraceEnd =
            HitResult.ImpactPoint;


        AActor* HitActor =
            HitResult.GetActor();


        if (HitActor)
        {
            UGameplayStatics::ApplyPointDamage(
                HitActor,
                ShotDamage,
                WorldDirection,
                HitResult,
                GetController(),
                this,
                nullptr
            );
        }


        OnShotHit(
            HitResult
        );
    }


    // ------------------------------------------------------------
    // GENERAL SHOT EVENT
    // ------------------------------------------------------------

    OnShotFired(
        TraceStart,
        FinalTraceEnd,
        bHit
    );


    // ------------------------------------------------------------
    // DEBUG
    // ------------------------------------------------------------

    if (bDrawShotDebug)
    {
        DrawDebugLine(
            GetWorld(),
            TraceStart,
            FinalTraceEnd,
            bHit
            ? FColor::Red
            : FColor::Green,
            false,
            1.0f,
            0,
            2.0f
        );


        if (bHit)
        {
            DrawDebugSphere(
                GetWorld(),
                HitResult.ImpactPoint,
                8.0f,
                12,
                FColor::Red,
                false,
                1.0f
            );
        }
    }
}


// ================================================================
// RELOAD
// ================================================================

void AFingerGunPlayerCharacter::Reload()
{
    if (
        CurrentAmmo
        >=
        MagazineSize
        )
    {
        return;
    }


    CurrentAmmo =
        MagazineSize;


    OnReloaded();
}


// ================================================================
// AMMO GETTER
// ================================================================

int32 AFingerGunPlayerCharacter::GetCurrentAmmo() const
{
    return CurrentAmmo;
}