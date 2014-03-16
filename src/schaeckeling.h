#define DMX_CHANNELS 512
#define MIDI_CHANNELS 128
#define INPUT_CHANNELS (DMX_CHANNELS+MIDI_CHANNELS)

typedef int inputidx_t;
typedef int dmxchannel_t;
typedef int midichannel_t;

static inline inputidx_t
midi_to_input_index(midichannel_t channel) {
	assert(channel >= 0 && channel < MIDI_CHANNELS);
	return channel;
}

static inline inputidx_t
dmx_to_input_index(dmxchannel_t channel) {
	assert(channel > 0 && channel <= DMX_CHANNELS);
	return channel + MIDI_CHANNELS - 1;
}

static inline midichannel_t
input_index_to_midi(inputidx_t input) {
	assert(input >= 0 && input < MIDI_CHANNELS);
	return input;
}

static inline dmxchannel_t
input_index_to_dmx(inputidx_t input) {
	assert(input >= MIDI_CHANNELS && input < INPUT_CHANNELS);
	return input - MIDI_CHANNELS + 1;
}

static int inline
input_index_is_midi(inputidx_t iidx) {
	assert(iidx >= 0 && iidx < INPUT_CHANNELS);
	return (iidx < MIDI_CHANNELS);
}

static int inline
input_index_is_dmx(inputidx_t iidx) {
	assert(iidx >= 0 && iidx < INPUT_CHANNELS);
	return (iidx >= MIDI_CHANNELS);
}
