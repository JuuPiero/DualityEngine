#pragma once

// One include for C++ gameplay scripts -- the counterpart of Unity's `using UnityEngine;`.
//
//   #include "DualityEngine/Scripting/Scripting.h"
//
//   class Spinner : public Duality::Behaviour { ... };
//
// Everything here is header-only on purpose: the desktop GameScripts DLL does not link
// DualityEngine and reaches the running engine only through EngineServices / ScriptContext.
// Including the individual headers (MeshRenderer.h, Rigidbody2D.h...) keeps working; this
// file only gathers them. Engine internals (Scene systems, renderers, serializers) are not
// part of the scripting API and are deliberately not included.

// Base class, DUALITY_PROPERTY / DUALITY_PROPERTIES_AUTO, Transform, ScriptContext.
#include "DualityEngine/Scene/Behaviour.h"
#include "DualityEngine/Scene/PointerEventHandlers.h"

// Component wrappers (Unity: GetComponent<T>() results).
#include "DualityEngine/Scripting/AudioSource.h"
#include "DualityEngine/Scripting/Camera.h"
#include "DualityEngine/Scripting/CharacterController.h"
#include "DualityEngine/Scripting/Collider2D.h"
#include "DualityEngine/Scripting/Collider3D.h"
#include "DualityEngine/Scripting/LineRenderer.h"
#include "DualityEngine/Scripting/MeshFilter.h"
#include "DualityEngine/Scripting/MeshRenderer.h"
#include "DualityEngine/Scripting/Rigidbody2D.h"
#include "DualityEngine/Scripting/Rigidbody3D.h"
#include "DualityEngine/Scripting/SpriteFlipbook.h"
#include "DualityEngine/Scripting/SpriteRenderer.h"
#include "DualityEngine/Scripting/Transform.h"
#include "DualityEngine/Scripting/UIWidgets.h"

// Static services (Unity: Input, Time, Debug, Physics, Physics2D, SceneManager, AudioSource.PlayClipAtPoint).
#include "DualityEngine/Scripting/Debug.h"
#include "DualityEngine/Scripting/EntityLayer.h"
#include "DualityEngine/Scripting/Input.h"
#include "DualityEngine/Scripting/ScriptAudio.h"
#include "DualityEngine/Scripting/ScriptDebug.h"
#include "DualityEngine/Scripting/ScriptPhysics2D.h"
#include "DualityEngine/Scripting/ScriptPhysics3D.h"
#include "DualityEngine/Scripting/ScriptScene.h"
#include "DualityEngine/Scripting/Time.h"

// Data assets (Unity: ScriptableObject).
#include "DualityEngine/Scripting/ScriptableObject.h"
