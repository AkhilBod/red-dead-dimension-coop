#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FGTrackerInput.generated.h"

class FSocket;
class UTexture2D;

/** /fg/state, in wire order. See PLAN.md section 4. */
struct FFGTrackerState
{
    float AimX = 0.5f;
    float AimY = 0.5f;
    bool bAimValid = false;
    bool bGunPose = false;
    bool bHolstered = false;
    float Lean = 0.0f;
    float Duck = 0.0f;
    float BodySpeed = 0.0f;
    bool bTracking = false;
    bool bOffHandOpen = false;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FFGFireEvent, FVector2D);
DECLARE_MULTICAST_DELEGATE(FFGReloadEvent);

/**
 * The seam to the Python tracker. OSC over UDP on 7000 (state, fire, reload), commands back on 7001,
 * and the tracker's camera preview as JPEG fragments on 7002.
 * With no tracker running it falls back to mouse and keys, so the game never needs a camera:
 * mouse aim, LMB fire, R reload, A/D lean, S duck, H holster, F focus.
 */
UCLASS(ClassGroup = (FingerGun), meta = (BlueprintSpawnableComponent))
class FINGERGUNGAME_API UFGTrackerInput : public UActorComponent
{
    GENERATED_BODY()

public:
    UFGTrackerInput();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    /** Smoothed up to frame rate. */
    FFGTrackerState State;

    /** True while /fg/state packets are arriving. False = mouse mode. */
    bool bTrackerLive = false;

    /** Seconds with tracking == 0 while the tracker is live. */
    float NoPersonSeconds = 0.0f;

    FFGFireEvent OnFire;
    FFGReloadEvent OnReload;

    void SendCalibBegin();
    void SendCalibTarget(float ScreenX, float ScreenY);
    void SendRecenter();
    void SendAimMode(bool bFinger);

    /** Space: the player has moved or the camera got bumped. Here and now becomes neutral stance and screen centre. */
    void Recenter();

    bool bFingerMode = false;       // P toggles: crosshair sits on the fingertip in the camera picture
    double RecenteredAt = -1000.0;

    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> CameraTexture;

    bool HasCameraPreview() const;

    /** Tracker aim is stretched about the screen centre by this much, so the edges need less reach. Mouse is untouched. */
    float EdgeGain = 1.0f;      // 1.3 was tried and played far too twitchy: it multiplies jitter as well as reach

    int32 StatePort = 7000;
    int32 CommandPort = 7001;
    int32 CameraPort = 7002;

private:
    FSocket* StateSocket = nullptr;
    FSocket* CameraSocket = nullptr;
    FSocket* SendSocket = nullptr;

    FFGTrackerState Raw;
    double LastStateTime = -1000.0;
    double LastCameraTime = -1000.0;
    double LastAimValidTime = -1000.0;

    uint16 CamFrameId = 0;
    int32 CamPartsGot = 0;
    TArray<TArray<uint8>> CamParts;

    void PollState();
    void PollCamera();
    void HandleOsc(const uint8* Data, int32 Size);
    void DecodeCameraFrame();
    void SendOsc(const char* Address, const TArray<float>& Args);
    void TickMouse(float DeltaTime);
    float Stretch(float V) const { return FMath::Clamp(0.5f + (V - 0.5f) * EdgeGain, 0.0f, 1.0f); }
};
