// Registration of Scene/Components/Audio.h. Display and field names are serialized: treat them
// as a data contract (see ComponentRegistration.h).
#include "ComponentRegistration.h"

namespace Duality::ComponentRegistration {

    void RegisterAudioComponents() {
        Component<AudioSourceComponent>("Audio Source", ComponentCategory::Audio, {
            MakeField("Enabled", &AudioSourceComponent::Enabled),
            MakeField("Clip", &AudioSourceComponent::Clip),
            MakeField("Loop", &AudioSourceComponent::Loop),
            MakeField("Volume", &AudioSourceComponent::Volume),
            MakeField("Play On Awake", &AudioSourceComponent::PlayOnAwake),
            MakeField("Mute", &AudioSourceComponent::Mute),
            MakeField("Spatial Blend", &AudioSourceComponent::SpatialBlend),
            MakeField("Min Distance", &AudioSourceComponent::MinDistance),
            MakeField("Max Distance", &AudioSourceComponent::MaxDistance),
        });

        Component<AudioListenerComponent>("Audio Listener", ComponentCategory::Audio, {
            MakeField("Enabled", &AudioListenerComponent::Enabled),
        });
    }

}
