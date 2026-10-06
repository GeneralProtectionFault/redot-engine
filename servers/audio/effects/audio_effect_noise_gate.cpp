#include "audio_effect_noise_gate.h"

#include "core/math/math_funcs.h"
#include "servers/audio/audio_server.h"

void AudioEffectNoiseGateInstance::process(const AudioFrame *p_src_frames, AudioFrame *p_dst_frames, int p_frame_count) {
	float mix_rate = AudioServer::get_singleton()->get_mix_rate();

	// Snapshot params once per buffer (audio thread reads them unsynchronized).
	float threshold = Math::db_to_linear(base->threshold_db);
	float floor_gain = Math::db_to_linear(base->attenuation_db);

	// One-pole coefficients from time constants.
	float attack_coeff = Math::exp(-1.0f / (MAX(base->attack_ms, 0.1f) * 0.001f * mix_rate));
	float release_coeff = Math::exp(-1.0f / (MAX(base->release_ms, 0.1f) * 0.001f * mix_rate));

	float env = envelope;
	float g = gain;

	for (int i = 0; i < p_frame_count; i++) {
		float level = MAX(Math::abs(p_src_frames[i].left), Math::abs(p_src_frames[i].right));

		// Envelope: fast-ish follow up, release-limited decay.
		env = level > env ? level : env * release_coeff;

		// Gate target: full gain above threshold, attenuation floor below.
		float target = env >= threshold ? 1.0f : floor_gain;

		// Attack when opening, release when closing.
		float coeff = target > g ? attack_coeff : release_coeff;
		g = target + (g - target) * coeff;

		p_dst_frames[i] = p_src_frames[i] * g;
	}

	envelope = env;
	gain = g;
}

Ref<AudioEffectInstance> AudioEffectNoiseGate::instantiate() {
	Ref<AudioEffectNoiseGateInstance> ins;
	ins.instantiate();
	ins->base = Ref<AudioEffectNoiseGate>(this);
	return ins;
}

void AudioEffectNoiseGate::set_threshold_db(float p_threshold_db) {
	threshold_db = CLAMP(p_threshold_db, -80.0f, 0.0f);
}

float AudioEffectNoiseGate::get_threshold_db() const {
	return threshold_db;
}

void AudioEffectNoiseGate::set_attack_ms(float p_attack_ms) {
	attack_ms = CLAMP(p_attack_ms, 0.1f, 1000.0f);
}

float AudioEffectNoiseGate::get_attack_ms() const {
	return attack_ms;
}

void AudioEffectNoiseGate::set_release_ms(float p_release_ms) {
	release_ms = CLAMP(p_release_ms, 1.0f, 5000.0f);
}

float AudioEffectNoiseGate::get_release_ms() const {
	return release_ms;
}

void AudioEffectNoiseGate::set_attenuation_db(float p_attenuation_db) {
	attenuation_db = CLAMP(p_attenuation_db, -80.0f, 0.0f);
}

float AudioEffectNoiseGate::get_attenuation_db() const {
	return attenuation_db;
}

void AudioEffectNoiseGate::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_threshold_db", "threshold_db"), &AudioEffectNoiseGate::set_threshold_db);
	ClassDB::bind_method(D_METHOD("get_threshold_db"), &AudioEffectNoiseGate::get_threshold_db);
	ClassDB::bind_method(D_METHOD("set_attack_ms", "attack_ms"), &AudioEffectNoiseGate::set_attack_ms);
	ClassDB::bind_method(D_METHOD("get_attack_ms"), &AudioEffectNoiseGate::get_attack_ms);
	ClassDB::bind_method(D_METHOD("set_release_ms", "release_ms"), &AudioEffectNoiseGate::set_release_ms);
	ClassDB::bind_method(D_METHOD("get_release_ms"), &AudioEffectNoiseGate::get_release_ms);
	ClassDB::bind_method(D_METHOD("set_attenuation_db", "attenuation_db"), &AudioEffectNoiseGate::set_attenuation_db);
	ClassDB::bind_method(D_METHOD("get_attenuation_db"), &AudioEffectNoiseGate::get_attenuation_db);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "threshold_db", PROPERTY_HINT_RANGE, "-80,0,0.1,suffix:dB"), "set_threshold_db", "get_threshold_db");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "attack_ms", PROPERTY_HINT_RANGE, "0.1,1000,0.1,suffix:ms"), "set_attack_ms", "get_attack_ms");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "release_ms", PROPERTY_HINT_RANGE, "1,5000,1,suffix:ms"), "set_release_ms", "get_release_ms");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "attenuation_db", PROPERTY_HINT_RANGE, "-80,0,0.1,suffix:dB"), "set_attenuation_db", "get_attenuation_db");
}

AudioEffectNoiseGate::AudioEffectNoiseGate() {
}
