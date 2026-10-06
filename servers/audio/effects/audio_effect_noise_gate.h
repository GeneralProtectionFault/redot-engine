#pragma once

#include "servers/audio/audio_effect.h"

class AudioEffectNoiseGate;

class AudioEffectNoiseGateInstance : public AudioEffectInstance {
	GDCLASS(AudioEffectNoiseGateInstance, AudioEffectInstance);
	friend class AudioEffectNoiseGate;
	Ref<AudioEffectNoiseGate> base;

	float envelope = 0.0f; ///< input level follower (linear)
	float gain = 0.0f;     ///< current applied gain (linear)

public:
	virtual void process(const AudioFrame *p_src_frames, AudioFrame *p_dst_frames, int p_frame_count) override;
};

class AudioEffectNoiseGate : public AudioEffect {
	GDCLASS(AudioEffectNoiseGate, AudioEffect);

	friend class AudioEffectNoiseGateInstance;

	float threshold_db = -40.0f;
	float attack_ms = 10.0f;   ///< Time for gate to open
	float release_ms = 200.0f; ///< Time for gate to close
	float attenuation_db = -60.0f; ///< Gain floor (how much the signal is reduced) when gate is closed. -80 = full mute

protected:
	static void _bind_methods();

public:
	Ref<AudioEffectInstance> instantiate() override;

	void set_threshold_db(float p_threshold_db);
	float get_threshold_db() const;

	void set_attack_ms(float p_attack_ms);
	float get_attack_ms() const;

	void set_release_ms(float p_release_ms);
	float get_release_ms() const;

	void set_attenuation_db(float p_attenuation_db);
	float get_attenuation_db() const;

	AudioEffectNoiseGate();
};
