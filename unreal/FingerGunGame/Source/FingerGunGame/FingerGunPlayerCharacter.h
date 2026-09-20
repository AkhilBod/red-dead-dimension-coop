#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "FingerGunPlayerCharacter.generated.h"

class UCameraComponent;
class UUserWidget;

UCLASS()
class FINGERGUNGAME_API AFingerGunPlayerCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    AFingerGunPlayerCharacter();

    virtual void Tick(float DeltaTime) override;

protected:
    virtual void BeginPlay() override;


public:

    // ============================================================
    // COMPONENTS
    // ============================================================

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Finger Gun|Components"
    )
    TObjectPtr<UCameraComponent> FirstPersonCamera;


    // ============================================================
    // BODY INPUT
    // ============================================================

    /**
     * -1 = full left
     *  0 = center
     * +1 = full right
     */
    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Finger Gun|Body Input"
    )
    float Lean = 0.0f;

    /**
     * 0 = fully crouched
     * 1 = fully standing
     */
    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Finger Gun|Body Input"
    )
    float Height = 1.0f;


    // ============================================================
    // BODY SETTINGS
    // ============================================================

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Finger Gun|Body",
        meta = (ClampMin = "0.0")
    )
    float LeanDistance = 60.0f;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Finger Gun|Body",
        meta = (ClampMin = "1.0")
    )
    float StandingCapsuleHalfHeight = 96.0f;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Finger Gun|Body",
        meta = (ClampMin = "1.0")
    )
    float CrouchedCapsuleHalfHeight = 48.0f;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Finger Gun|Body"
    )
    float StandingCameraZ = 64.0f;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Finger Gun|Body"
    )
    float CrouchedCameraZ = 28.0f;


    // ============================================================
    // BODY FUNCTIONS
    // ============================================================

    UFUNCTION(
        BlueprintCallable,
        Category = "Finger Gun|Body Input"
    )
    void SetLean(float NewLean);

    UFUNCTION(
        BlueprintCallable,
        Category = "Finger Gun|Body Input"
    )
    void SetHeight(float NewHeight);

    UFUNCTION(
        BlueprintCallable,
        Category = "Finger Gun|Body Input"
    )
    void SetBodyInput(float NewLean, float NewHeight);

    UFUNCTION(
        BlueprintCallable,
        Category = "Finger Gun|Body Input"
    )
    void ResetBodyInput();

    UFUNCTION(
        BlueprintPure,
        Category = "Finger Gun|Body Input"
    )
    float GetLean() const;

    UFUNCTION(
        BlueprintPure,
        Category = "Finger Gun|Body Input"
    )
    float GetHeight() const;


    // ============================================================
    // CROSSHAIR WIDGET
    // ============================================================

    /**
     * Set this in BP_FingerGunPlayer to WBP_Crosshair.
     */
    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Finger Gun|Aim|Crosshair"
    )
    TSubclassOf<UUserWidget> CrosshairWidgetClass;

    UPROPERTY(
        BlueprintReadOnly,
        Category = "Finger Gun|Aim|Crosshair"
    )
    TObjectPtr<UUserWidget> CrosshairWidget;


    // ============================================================
    // AIM SETTINGS
    // ============================================================

    /**
     * Used only for mouse/debug aiming.
     *
     * CV should use SetAimNormalized directly.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Finger Gun|Aim"
    )
    float AimSensitivity = 0.02f;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Finger Gun|Aim"
    )
    bool bInvertAimY = false;

    /**
     * Prevents the crosshair from touching the absolute
     * edge of the viewport.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Finger Gun|Aim|Crosshair",
        meta = (ClampMin = "0.0")
    )
    float CrosshairScreenMargin = 25.0f;


    // ============================================================
    // AIM STATE
    // ============================================================

    /**
     * -1 = left
     *  0 = center
     * +1 = right
     */
    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Finger Gun|Aim"
    )
    float AimX = 0.0f;

    /**
     * -1 = bottom
     *  0 = center
     * +1 = top
     */
    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Finger Gun|Aim"
    )
    float AimY = 0.0f;

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Finger Gun|Aim"
    )
    FVector2D CrosshairScreenPosition = FVector2D::ZeroVector;


    // ============================================================
    // AIM FUNCTIONS
    // ============================================================

    /**
     * DEBUG MOUSE INPUT
     *
     * Feed IA_Look's Action Value directly into this.
     */
    UFUNCTION(
        BlueprintCallable,
        Category = "Finger Gun|Aim"
    )
    void AddAimInput(FVector2D InputDelta);

    /**
     * CV INPUT
     *
     * Pass absolute normalized coordinates here.
     *
     * X: -1 left -> +1 right
     * Y: -1 bottom -> +1 top
     */
    UFUNCTION(
        BlueprintCallable,
        Category = "Finger Gun|Aim"
    )
    void SetAimNormalized(float NewAimX, float NewAimY);

    UFUNCTION(
        BlueprintCallable,
        Category = "Finger Gun|Aim"
    )
    void ResetAim();

    UFUNCTION(
        BlueprintPure,
        Category = "Finger Gun|Aim"
    )
    FVector2D GetAimNormalized() const;

    UFUNCTION(
        BlueprintPure,
        Category = "Finger Gun|Aim"
    )
    FVector2D GetCrosshairScreenPosition() const;


    // ============================================================
    // WEAPON SETTINGS
    // ============================================================

    /**
     * Damage dealt by one successful hit.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Finger Gun|Weapon"
    )
    float ShotDamage = 25.0f;

    /**
     * Maximum hitscan distance.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Finger Gun|Weapon"
    )
    float ShotRange = 100000.0f;

    /**
     * Minimum seconds between shots.
     *
     * 0.25 = maximum four shots per second.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Finger Gun|Weapon",
        meta = (ClampMin = "0.0")
    )
    float FireCooldown = 0.25f;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Finger Gun|Weapon",
        meta = (ClampMin = "1")
    )
    int32 MagazineSize = 6;

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Finger Gun|Weapon"
    )
    int32 CurrentAmmo = 6;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Finger Gun|Weapon"
    )
    bool bInfiniteAmmo = false;

    /**
     * Draw the actual shot line so we can debug
     * whether the crosshair is hitting correctly.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Finger Gun|Weapon|Debug"
    )
    bool bDrawShotDebug = true;


    // ============================================================
    // WEAPON FUNCTIONS
    // ============================================================

    /**
     * Attempts to fire.
     *
     * Handles:
     * - cooldown
     * - ammo
     * - crosshair deprojection
     * - hitscan
     * - damage
     */
    UFUNCTION(
        BlueprintCallable,
        Category = "Finger Gun|Weapon"
    )
    void Fire();

    /**
     * For now this instantly fills the magazine.
     *
     * We can add a reload delay/animation next.
     */
    UFUNCTION(
        BlueprintCallable,
        Category = "Finger Gun|Weapon"
    )
    void Reload();

    UFUNCTION(
        BlueprintPure,
        Category = "Finger Gun|Weapon"
    )
    bool CanFire() const;

    UFUNCTION(
        BlueprintPure,
        Category = "Finger Gun|Weapon"
    )
    int32 GetCurrentAmmo() const;


    // ============================================================
    // BLUEPRINT WEAPON EVENTS
    // ============================================================

    /**
     * Called whenever an actual shot is fired.
     *
     * Use this later for:
     * - muzzle flash
     * - gun sound
     * - Niagara tracer
     * - recoil
     */
    UFUNCTION(
        BlueprintImplementableEvent,
        Category = "Finger Gun|Weapon|Effects"
    )
    void OnShotFired(
        FVector TraceStart,
        FVector TraceEnd,
        bool bHit
    );

    /**
     * Called specifically when a shot hits something.
     *
     * Use this for impact particles / sounds.
     */
    UFUNCTION(
        BlueprintImplementableEvent,
        Category = "Finger Gun|Weapon|Effects"
    )
    void OnShotHit(
        const FHitResult& HitResult
    );

    /**
     * Called when firing with no ammo.
     */
    UFUNCTION(
        BlueprintImplementableEvent,
        Category = "Finger Gun|Weapon|Effects"
    )
    void OnDryFire();

    /**
     * Called whenever Reload() succeeds.
     */
    UFUNCTION(
        BlueprintImplementableEvent,
        Category = "Finger Gun|Weapon|Effects"
    )
    void OnReloaded();


protected:

    // ============================================================
    // INTERNAL BODY
    // ============================================================

    void UpdateBodyTransform();


    // ============================================================
    // INTERNAL CROSSHAIR
    // ============================================================

    void CreateCrosshairWidget();

    void UpdateCrosshairPosition();


    // ============================================================
    // INTERNAL WEAPON
    // ============================================================

    /**
     * Converts the current crosshair pixel position
     * into a ray in the 3D world.
     */
    bool GetCrosshairWorldRay(
        FVector& OutWorldOrigin,
        FVector& OutWorldDirection
    ) const;


    // ============================================================
    // STARTING BODY STATE
    // ============================================================

    FVector StartingActorLocation = FVector::ZeroVector;

    FVector StartingRightVector = FVector::RightVector;

    FVector StartingUpVector = FVector::UpVector;

    FVector StartingCameraRelativeLocation = FVector::ZeroVector;


    // ============================================================
    // INTERNAL WEAPON STATE
    // ============================================================

    float LastFireTime = -1000.0f;
};