// soh/soh/Enhancements/randomizer/ComboKeyframe.h — ComboShip: MM's flex keyframe skeletons (mm c_keyframe.c) in OOT,
// for MM effects drawn in Hyrule (the Goron crater, ComboCrater.c). Types and code are MM's, renamed Combo*; the
// skeleton / animation data is loaded by MM's own resource manager (Combo_MmRawResource), whose KeyFrame factories lay
// it out exactly as these structs.
#ifndef COMBO_KEYFRAME_H
#define COMBO_KEYFRAME_H

#ifdef __cplusplus
extern "C" {
#endif

// Include after global.h.
struct ComboKFSkelAnimeFlex;
struct ComboKFSkelAnime;

typedef s32 (*ComboOverrideKeyframeDraw)(struct PlayState* play, struct ComboKFSkelAnime* kfSkelAnime, s32 limbIndex,
                                         Gfx** dList, u8* flags, void* arg, Vec3s* rot, Vec3f* pos);
typedef s32 (*ComboPostKeyframeDraw)(struct PlayState* play, struct ComboKFSkelAnime* kfSkelAnime, s32 limbIndex,
                                     Gfx** dList, u8* flags, void* arg, Vec3s* rot, Vec3f* pos);

typedef s32 (*ComboOverrideKeyframeDrawScaled)(struct PlayState* play, struct ComboKFSkelAnimeFlex* kfSkelAnime,
                                               s32 limbIndex, Gfx** dList, u8* flags, void* arg, Vec3f* scale,
                                               Vec3s* rot, Vec3f* pos);
typedef s32 (*ComboPostKeyframeDrawScaled)(struct PlayState* play, struct ComboKFSkelAnimeFlex* kfSkelAnime,
                                           s32 limbIndex, Gfx** dList, u8* flags, void* arg, Vec3f* scale, Vec3s* rot,
                                           Vec3f* pos);

typedef s32 (*ComboKeyframeTransformCallback)(struct PlayState* play, struct ComboKFSkelAnimeFlex* kfSkelAnime,
                                              s32 limbIndex, Gfx** dList, u8* flags, void* arg);

// These flags are mutually exclusive, if XLU is set OPA is not set and vice-versa.
#define KEYFRAME_DRAW_OPA (0 << 0)
#define KEYFRAME_DRAW_XLU (1 << 0)

typedef enum {
    /* 0 */ KEYFRAME_NOT_DONE,
    /* 1 */ KEYFRAME_DONE_ONCE,
    /* 2 */ KEYFRAME_DONE_LOOP
} ComboKeyFrameDoneType;

#define KF_CALLBACK_INDEX_NONE 0xFF

typedef struct {
    /* 0x00 */ Gfx* dList;     // Display list for this limb
    /* 0x04 */ u8 numChildren; // Number of children for this limb
    /* 0x05 */ u8
        drawFlags; // KEYFRAME_DRAW_*, determines which render layer the matrix + display list will be appended to
    /* 0x06 */ Vec3s jointPos; // For the root limb this is the position in model space; for child limbs it is the
                               // relative position to the parent
} ComboKeyFrameStandardLimb;   // size = 0xC

typedef struct {
    /* 0x00 */ u8 limbCount;  // Number of limbs in this skeleton
    /* 0x01 */ u8 dListCount; // Number of limbs with a non-NULL display list, used to know how many matrices to
                              // allocate for drawing
    /* 0x04 */ ComboKeyFrameStandardLimb* limbs; // Pointer to standard limb array
} ComboKeyFrameSkeleton;                         // Size = 0x8

typedef struct {
    /* 0x00 */ Gfx* dList;     // Display list for this limb
    /* 0x04 */ u8 numChildren; // Number of children for this limb
    /* 0x05 */ u8
        drawFlags; // KEYFRAME_DRAW_*, determines which render layer the matrix + display list will be appended to
    /* 0x06 */ u8 callbackIndex; // Transform callback function index, KF_CALLBACK_INDEX_NONE indicates no callback
} ComboKeyFrameFlexLimb;         // size = 0x8

typedef struct {
    /* 0x00 */ u8 limbCount;  // Number of limbs in this skeleton
    /* 0x01 */ u8 dListCount; // Number of limbs with a non-NULL display list, used to know how many matrices to
                              // allocate for drawing
    /* 0x04 */ ComboKeyFrameFlexLimb* limbs; // Pointer to flex limb array
} ComboKeyFrameFlexSkeleton;                 // Size = 0x8

typedef struct {
    /* 0x00 */ s16 frame;    // Frame number for this keyframe
    /* 0x02 */ s16 value;    // Value (any of translation, rotation, scale)
    /* 0x04 */ s16 velocity; // The instantaneous rate of change of the value
} ComboKeyFrame;             // Size = 0x6

typedef struct {
    // Array of bitflags for each limb indicating whether to do keyframe interpolation
    // or pull from fixed values that do not change throughout the animation.
    union {
        // For standard the bit layout in each array element is:
        // [5] X Translation (root limb only)
        // [4] Y Translation (root limb only)
        // [3] Z Translation (root limb only)
        // [2] X Rotation (all limbs)
        // [1] Y Rotation (all limbs)
        // [0] Z Rotation (all limbs)
        /* 0x00 */ u8* standard;
        // For flex the bit layout in each array element is:
        // [8] X Scale
        // [7] Y Scale
        // [6] Z Scale
        // [5] X Rotation
        // [4] Y Rotation
        // [3] Z Rotation
        // [2] X Translation
        // [1] Y Translation
        // [0] Z Translation
        /* 0x00 */ u16* flex;
    } bitFlags;
    /* 0x04 */ ComboKeyFrame* keyFrames; // Array of keyframes determining the motion, grouped by limb
    /* 0x08 */ s16* kfNums;              // Array containing how many keyframes belong to each limb
    /* 0x0C */ s16*
        fixedValues; // Array of fixed rotation (standard skeleton) or scale/rotation/translation (flex skeleton) values
    /* 0x10 */ s16 unk_10;
    /* 0x12 */ s16 frameCount; // Length of the animation in 30fps frames
} ComboKeyFrameAnimation;      // Size = 0x14

typedef enum {
    /* 0 */ KEYFRAME_ANIM_ONCE, // Play once and stop
    /* 1 */ KEYFRAME_ANIM_LOOP  // Play in a loop
} ComboKeyFrameAnimMode;

typedef struct {
    /* 0x00 */ f32 start;      // Current animation start frame number
    /* 0x04 */ f32 end;        // Current animation end frame number
    /* 0x08 */ f32 frameCount; // Current animation total frame count
    /* 0x0C */ f32 speed;      // Current play speed
    /* 0x10 */ f32 curTime;    // Current play frame number
    /* 0x14 */ s32 animMode;   // Current play mode (see FrameAnimMode)
} ComboFrameControl;           // Size = 0x18

typedef struct ComboKFSkelAnime {
    /* 0x00 */ ComboFrameControl frameCtrl;       // Current play state
    /* 0x18 */ ComboKeyFrameSkeleton* skeleton;   // Skeleton to animate
    /* 0x1C */ ComboKeyFrameAnimation* animation; // Currently active animation
    /* 0x20 */ f32
        morphFrames; // Number of frames in which to morph between the previous pose and the current animation
    /* 0x24 */ Vec3s* jointTable; // Array of data describing the current pose
                                  //     size = 1 + limbCount, one root translation followed by rotations for each limb
    /* 0x28 */ Vec3s* morphTable; // Array of data describing the current morph pose to interpolate with
                                  //     size = 1 + limbCount, one root translation followed by rotations for each limb
    /* 0x2C */ Vec3s* rotOffsetsTable; // Table of rotations to add to the current pose, may be NULL so that no
                                       // additional rotations are added
                                       //     size = limbCount
} ComboKFSkelAnime;                    // Size = 0x30

typedef struct ComboKFSkelAnimeFlex {
    /* 0x00 */ ComboFrameControl frameCtrl;                        // Current play state
    /* 0x18 */ ComboKeyFrameFlexSkeleton* skeleton;                // Skeleton to animate
    /* 0x1C */ ComboKeyFrameAnimation* animation;                  // Currently active animation
    /* 0x20 */ ComboKeyframeTransformCallback* transformCallbacks; // Pointer to array of limb transform callbacks,
                                                                   // indexed by callbackIndex in ComboKeyFrameFlexLimb
    /* 0x24 */ f32
        morphFrames; // Number of frames in which to morph between the previous pose and the current animation
    /* 0x28 */ Vec3s* jointTable; // Array of data describing the current pose
                                  //    size = 3 * limbCount in order of (scale, rotation, translation) for each limb
    /* 0x2C */ Vec3s* morphTable; // Array of data describing the current morph pose to interpolate with
                                  //    size = 3 * limbCount in order of (scale, rotation, translation) for each limb
} ComboKFSkelAnimeFlex;           // Size = 0x30

void ComboKeyframe_InitFlex(ComboKFSkelAnimeFlex* kfSkelAnime, ComboKeyFrameFlexSkeleton* skeleton,
                            ComboKeyFrameAnimation* animation, Vec3s* jointTable, Vec3s* morphTable,
                            ComboKeyframeTransformCallback* transformCallbacks);
void ComboKeyframe_DestroyFlex(ComboKFSkelAnimeFlex* kfSkelAnime);
void ComboKeyframe_FlexPlayOnce(ComboKFSkelAnimeFlex* kfSkelAnime, ComboKeyFrameAnimation* animation);
s32 ComboKeyframe_UpdateFlex(ComboKFSkelAnimeFlex* kfSkelAnime);
void ComboKeyframe_DrawFlex(struct PlayState* play, ComboKFSkelAnimeFlex* kfSkelAnime, Mtx* mtxStack,
                            ComboOverrideKeyframeDrawScaled overrideKeyframeDraw,
                            ComboPostKeyframeDrawScaled postKeyframeDraw, void* arg);

#ifdef __cplusplus
}
#endif

#endif
