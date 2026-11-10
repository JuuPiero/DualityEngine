#pragma once

// Every built-in component, by domain. Include this umbrella (or one domain header) to use them.
//
//   Components/Core.h         Name, Tag, Active, Transform, Hierarchy, Prefab Instance, Layer, Camera
//   Components/Rendering2D.h  Sprite Renderer, Tilemap, Line / Trail Renderer, Text Mesh, Particle System
//   Components/Rendering3D.h  Mesh Filter, Mesh Renderer, Skinned Mesh Renderer, lights, LOD Group, Billboard
//   Components/Animation.h    Animation, Sprite Flipbook, Sprite Sheet Animator
//   Components/Physics2D.h    Rigidbody 2D, 2D colliders / joints / effectors, Physics Raycaster 2D
//   Components/Physics3D.h    Rigidbody 3D, 3D colliders, Character Controller, joints, Physics Raycaster 3D
//   Components/UI.h           Canvas, UI Rect and every UI widget / layout / effect
//   Components/Audio.h        Audio Source, Audio Listener
//   Components/Constraints.h  Follow Target and the Position/Rotation/Scale/Parent/Look At/Aim constraints
//   Components/Scripting.h    Scripts (BehaviourComponent: C++ and TypeScript script slots)
//
// Conventions shared by every component:
// - Authored fields are plain members; each component is registered once in
//   DualityEngine/Source/Scene/Components/<Domain>Components.cpp (TypeRegistry). That registration
//   -- display name + field names -- IS the serialization contract (scenes, prefabs, copy/paste,
//   undo snapshots, TypeScript `getComponent('Name').get('Field')`): renaming is a format change.
// - Members documented as "runtime" are never registered/serialized and are reset at Play start.
// - Inspector enums are registered with MakeEnumField (serialized by label), so appending a value
//   is backward compatible and renaming one is not.
// - Axes: the world is +Y DOWN in 2D and 3D (gravity is +Y); every "up" is -Y unless a field says
//   otherwise. JSON has no infinity: "Break Force"/"Break Torque" use 0 = Infinity.

#include "DualityEngine/Scene/Components/Core.h"
#include "DualityEngine/Scene/Components/Rendering2D.h"
#include "DualityEngine/Scene/Components/Rendering3D.h"
#include "DualityEngine/Scene/Components/Animation.h"
#include "DualityEngine/Scene/Components/Physics2D.h"
#include "DualityEngine/Scene/Components/Physics3D.h"
#include "DualityEngine/Scene/Components/UI.h"
#include "DualityEngine/Scene/Components/Audio.h"
#include "DualityEngine/Scene/Components/Constraints.h"
#include "DualityEngine/Scene/Components/Scripting.h"
