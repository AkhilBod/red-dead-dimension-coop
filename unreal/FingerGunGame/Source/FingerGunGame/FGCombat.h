#pragma once

#include "CoreMinimal.h"
#include "FGNetTypes.h"

class AFGBandit;
class AFGTarget;
class UWorld;

/**
 * The parts of a shot that every machine needs: which thing a ray picks out (the host to decide it, the shooter to
 * show it at once), whether a point is on a player's screen, and the flashes, tracers and sounds.
 */
namespace FGCombat
{
    struct FShotHit
    {
        AFGBandit* Bandit = nullptr;
        AFGTarget* Target = nullptr;
        bool bHead = false;
        bool bGround = false;          // a miss that lands on the ground (kicks up dust)
        FVector Point = FVector::ZeroVector;
        /** Everything else inside the cone, best first: a shotgun's spread, a rifle going through. */
        TArray<TPair<float, AFGBandit*>> AlsoHit;
        bool Any() const { return Bandit || Target; }
    };

    /** Webcam aim is noisy, so anything within Assist degrees of the ray counts. Nearest to the ray wins. */
    FShotHit FindHit(UWorld* World, const FVector& Origin, const FVector& Dir, float Assist, bool bBossLocked, bool bAnyTarget);

    /** On screen for a camera at View with this horizontal field of view, inside a small margin. No viewport needed. */
    bool CanSee(const FTransform& View, float HorizontalFov, float Aspect, const FVector& WorldPoint);

    void Sfx(UWorld* World, FName Name, float Volume = 1.0f, float Pitch = 1.0f);
    void Fx(UWorld* World, EFGFxKind Kind, const FVector& At, float TrainSpeed);
    void PlayerShot(UWorld* World, const FVector& Muzzle, const FVector& HitPoint, int32 Tracers);
    void EnemyShot(UWorld* World, const FVector& From, const FVector& Target, float Flight);
    void HatOff(UWorld* World, const FVector& Head);
}
