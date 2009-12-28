#include <wav2prg_api.h>
#include <malloc.h>

struct wav2prg_context {
  wav2prg_get_rawpulse_func get_rawpulse;
  wav2prg_test_eof_func test_eof_func;
  wav2prg_get_pos_func get_pos_func;
  wav2prg_get_first_sync get_first_sync;
    struct wav2prg_functions subclassed_functions;
  struct wav2prg_tolerance* adaptive_tolerances;
  struct wav2prg_tolerance* strict_tolerances;
  void *audiotap;
  uint8_t checksum;
  enum wav2prg_checksum_state checksum_state;
  struct wav2prg_block block;
    uint16_t block_total_size;
    uint16_t block_current_size;
};

static enum wav2prg_return_values get_pulse_tolerant(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, uint8_t* pulse)
{
  uint32_t raw_pulse;
  enum wav2prg_return_values ret = context->get_rawpulse(context->audiotap, &raw_pulse);
  if (ret == wav2prg_invalid)
    return wav2prg_invalid;
  for (*pulse = 0; *pulse < conf->num_pulse_lengths - 1; *pulse++) {
    if (raw_pulse < conf->thresholds[*pulse])
      return wav2prg_ok;
  }
  return wav2prg_ok;
}

static enum wav2prg_return_values get_pulse_adaptively_tolerant(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, uint8_t* pulse)
{
  uint32_t raw_pulse;
  enum wav2prg_return_values ret = context->get_rawpulse(context->audiotap, &raw_pulse);
  if (ret == wav2prg_invalid)
    return wav2prg_invalid;
  *pulse = 0;
  if(raw_pulse < conf->ideal_pulse_lengths[*pulse] - context->adaptive_tolerances[*pulse].less_than_ideal)
  {
    if ( (raw_pulse * 100) / conf->ideal_pulse_lengths[*pulse] > 50)
    {
      context->adaptive_tolerances[*pulse].less_than_ideal = conf->ideal_pulse_lengths[*pulse] - raw_pulse + 1;
      return wav2prg_ok;
    }
    else
      return wav2prg_invalid;
  }
  while(1){
    int32_t distance_from_low = raw_pulse - conf->ideal_pulse_lengths[*pulse] - context->adaptive_tolerances[*pulse].more_than_ideal;
    int32_t distance_from_high;
    if (distance_from_low < 0)
      return wav2prg_ok;
    if (*pulse == conf->num_pulse_lengths - 1)
      break;
    distance_from_high = conf->ideal_pulse_lengths[*pulse + 1] - context->adaptive_tolerances[*pulse + 1].less_than_ideal - raw_pulse;
    if (distance_from_high > distance_from_low)
    {
      context->adaptive_tolerances[*pulse].more_than_ideal += distance_from_low + 1;
      return wav2prg_ok;
    }
    (*pulse)++;
    if (distance_from_high >= 0)
    {
      context->adaptive_tolerances[*pulse].less_than_ideal += distance_from_high + 1;
      return wav2prg_ok;
    }
  }
  if ( (raw_pulse * 100) / conf->ideal_pulse_lengths[*pulse] < 150)
  {
    context->adaptive_tolerances[*pulse].more_than_ideal = raw_pulse - conf->ideal_pulse_lengths[*pulse] + 1;
    return wav2prg_ok;
  }
  return wav2prg_invalid;
}

static enum wav2prg_return_values get_pulse_intolerant(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, uint8_t* pulse)
{
  uint32_t raw_pulse;
  enum wav2prg_return_values ret = context->get_rawpulse(context->audiotap, &raw_pulse);
  if (ret == wav2prg_invalid)
    return wav2prg_invalid;

    for(*pulse = 0; *pulse < conf->num_pulse_lengths; (*pulse)++){
    if (raw_pulse > conf->ideal_pulse_lengths[*pulse] - context->strict_tolerances[*pulse].less_than_ideal
     && raw_pulse < conf->ideal_pulse_lengths[*pulse] + context->strict_tolerances[*pulse].more_than_ideal)
      return wav2prg_ok;
  }
    return wav2prg_invalid;
}

static enum wav2prg_return_values get_bit_default(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, uint8_t* bit)
{
    uint8_t pulse;
    if (context->subclassed_functions.get_pulse_func(context, functions, conf, &pulse) == wav2prg_invalid)
        return wav2prg_invalid;
    switch(pulse) {
        case 0 : *bit = 0; return wav2prg_ok;
        case 1 : *bit = 1; return wav2prg_ok;
        default:           return wav2prg_invalid;
    }
}

static void wav2prg_reset_checksum_to(struct wav2prg_context* context, uint8_t byte)
{
  context->checksum = byte;
}

static void wav2prg_reset_checksum(struct wav2prg_context* context)
{
    wav2prg_reset_checksum_to(context, 0);
}

static void wav2prg_update_checksum(struct wav2prg_context* context, struct wav2prg_plugin_conf* conf, uint8_t byte)
{
  switch (conf->checksum_type) {
        case wav2prg_xor_checksum: context->checksum ^= byte; return;
        case wav2prg_add_checksum: context->checksum += byte; return;
        default:;
    }
}

static enum wav2prg_return_values evolve_byte(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, uint8_t* byte)
{
  uint8_t bit;
  enum wav2prg_return_values res = context->subclassed_functions.get_bit_func(context, functions, conf, &bit);
  if (res == wav2prg_invalid)
    return wav2prg_invalid;
  switch (conf->endianness) {
    case lsbf: *byte = (*byte >> 1) | (128 * bit); return wav2prg_ok;
    case msbf: *byte = (*byte << 1) |        bit ; return wav2prg_ok;
    default  : return wav2prg_invalid;
  }
}

static enum wav2prg_return_values get_byte_default(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, uint8_t* byte)
{
  uint8_t i;
  for(i = 0; i < 8; i++)
  {
    if(evolve_byte(context, functions, conf, byte) == wav2prg_invalid)
      return wav2prg_invalid;
  }

  return wav2prg_ok;
}

static enum wav2prg_return_values get_word_default(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, uint16_t* word)
{
  uint8_t byte;
  if (context->subclassed_functions.get_byte_func(context, functions, conf, &byte) == wav2prg_invalid)
    return wav2prg_invalid;
  *word = byte;
  if (context->subclassed_functions.get_byte_func(context, functions, conf, &byte) == wav2prg_invalid)
    return wav2prg_invalid;
  *word |= (byte << 8);
  return wav2prg_ok;
}

static enum wav2prg_return_values get_word_bigendian_default(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, uint16_t* word)
{
  uint8_t byte;
  if (context->subclassed_functions.get_byte_func(context, functions, conf, &byte) == wav2prg_invalid)
    return wav2prg_invalid;
  *word = (byte << 8);
  if (context->subclassed_functions.get_byte_func(context, functions, conf, &byte) == wav2prg_invalid)
    return wav2prg_invalid;
  *word |= byte;
  return wav2prg_ok;
}

static enum wav2prg_return_values get_block_default(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, uint16_t* block_size, uint16_t* skipped_at_beginning)
{
    uint16_t bytes_received;

    *skipped_at_beginning = 0;
    for(bytes_received = 0; bytes_received != *block_size;bytes_received++){
        uint8_t byte;
        if (context->subclassed_functions.get_byte_func(context, functions, conf, &byte) == wav2prg_invalid) {
            *block_size = bytes_received;
            return wav2prg_invalid;
        }
        functions->add_byte_to_block(context, conf, byte);
    }
    return wav2prg_ok;
}

static enum wav2prg_sync_return_values sync_to_bit(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, uint8_t* byte)
{
    uint8_t bit;
    do{
        enum wav2prg_return_values res = context->subclassed_functions.get_bit_func(context, functions, conf, &bit);
        if (res == wav2prg_invalid)
            return wav2prg_notsynced;
    }while(bit != conf->pilot_byte);
    return wav2prg_synced;
}

static enum wav2prg_sync_return_values sync_to_byte(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf, uint8_t* byte)
{
  *byte = 0;
  do{
    if(evolve_byte(context, functions, conf, byte) == wav2prg_invalid)
      return wav2prg_notsynced;
  }while(*byte != conf->pilot_byte);
  do{
    if (context->subclassed_functions.get_byte_func(context, functions, conf, byte) == wav2prg_invalid)
      return wav2prg_notsynced;
  }while(*byte == conf->pilot_byte);
  return wav2prg_synced_and_one_byte_got;
}

static enum wav2prg_return_values get_sync(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf)
{
  uint32_t bytes_sync;
  do{
    uint8_t byte;
    enum wav2prg_sync_return_values res = context->get_first_sync(context, functions, conf, &byte);
    uint8_t byte_to_consume = res == wav2prg_synced_and_one_byte_got;
    if(res == wav2prg_notsynced)
      return wav2prg_invalid;
    for(bytes_sync = 0; bytes_sync < conf->len_of_pilot_sequence; bytes_sync++){
      if(byte_to_consume)
        byte_to_consume = 0;
      else if(context->subclassed_functions.get_byte_func(context, functions, conf, &byte) == wav2prg_invalid)
        return wav2prg_invalid;
      if (byte != conf->pilot_sequence[bytes_sync])
        break;
    }
  }while(bytes_sync != conf->len_of_pilot_sequence);
  return wav2prg_ok;
}

static void check_checksum_against_default(struct wav2prg_context* context, uint8_t loaded_checksum)
{
  context->checksum_state = context->checksum == loaded_checksum ? wav2prg_checksum_state_correct : wav2prg_checksum_state_load_error;
}

static enum wav2prg_return_values check_checksum_default(struct wav2prg_context* context, const struct wav2prg_functions* functions, struct wav2prg_plugin_conf* conf)
{
  uint8_t loaded_checksum;
  
  printf("computed checksum %u (%02x)", context->checksum, context->checksum);
  if (context->subclassed_functions.get_byte_func(context, functions, conf, &loaded_checksum))
    return wav2prg_invalid;
  printf("loaded checksum %u (%02x)\n", loaded_checksum, loaded_checksum);
  check_checksum_against_default(context, loaded_checksum);
  return wav2prg_ok;
}

static void add_byte_to_block_default(struct wav2prg_context *context, struct wav2prg_plugin_conf* conf, uint8_t byte)
{
    context->block.data[context->block_current_size++] = byte;
    wav2prg_update_checksum(context, conf, byte);
}

void wav2prg_get_new_context(wav2prg_get_rawpulse_func rawpulse_func,
              wav2prg_test_eof_func test_eof_func,
              wav2prg_get_pos_func get_pos_func,
              enum wav2prg_tolerance_type tolerance_type,
              struct wav2prg_plugin_conf* conf,
			  const struct wav2prg_plugin_functions* plugin_functions,
              struct wav2prg_tolerance* tolerances,
              void* audiotap)
{
  struct wav2prg_context context =
  {
    rawpulse_func,
    test_eof_func,
    get_pos_func,
    plugin_functions->get_first_sync ? plugin_functions->get_first_sync :
    conf->findpilot_type == wav2prg_synconbit ? sync_to_bit : sync_to_byte,
    {
      get_sync,
      get_pulse_intolerant,
      plugin_functions->get_bit_func ? plugin_functions->get_bit_func : get_bit_default,
      plugin_functions->get_byte_func ? plugin_functions->get_byte_func : get_byte_default,
      get_word_default,
      get_word_bigendian_default,
      plugin_functions->get_block_func ? plugin_functions->get_block_func : get_block_default,
      plugin_functions->check_checksum_func ? plugin_functions->check_checksum_func : check_checksum_default,
      check_checksum_against_default,
      add_byte_to_block_default
    },
    tolerance_type == wav2prg_adaptively_tolerant ?
      calloc(1, sizeof(struct wav2prg_tolerance) * conf->num_pulse_lengths) : NULL,
    tolerances,
    audiotap,
    0,
    wav2prg_no_checksum
  };
  struct wav2prg_functions functions =
  {
    get_sync,
    get_pulse_intolerant,
    get_bit_default,
    get_byte_default,
    get_word_default,
    get_word_bigendian_default,
    get_block_default,
    check_checksum_default,
    check_checksum_against_default,
    add_byte_to_block_default
  };

  while(!context.test_eof_func(context.audiotap)){
    uint16_t skipped_at_beginning, real_block_size;
    enum wav2prg_return_values res;
    int32_t pos;

    context.checksum = 0;
    functions.get_pulse_func = get_pulse_intolerant;
    pos = get_pos_func(audiotap);
    res = get_sync(&context, &functions, conf);
    if(res != wav2prg_ok)
      continue;
    functions.get_pulse_func = tolerance_type == wav2prg_tolerant ? get_pulse_tolerant :
    tolerance_type == wav2prg_adaptively_tolerant ?
    get_pulse_adaptively_tolerant : get_pulse_intolerant;

    printf("found something starting at %d \n",pos);
    pos = get_pos_func(audiotap);
    printf("and syncing at %d \n",pos);
    res = plugin_functions->get_block_info(&context, &functions, conf, context.block.name, &context.block.start, &context.block.end);
    if(res != wav2prg_ok)
      continue;
    if(context.block.end < context.block.start && context.block.end != 0)
      continue;
    pos = get_pos_func(audiotap);
    printf("block info ends at %d\n",pos);
    real_block_size = context.block_total_size = context.block.end - context.block.start;
    context.block_current_size = 0;
    res = context.subclassed_functions.get_block_func(&context, &functions, conf, &real_block_size, &skipped_at_beginning);
    if(res != wav2prg_ok)
      continue;
    pos = get_pos_func(audiotap);
    printf("checksum starts at %d \n",pos);
    context.subclassed_functions.check_checksum_func(&context, &functions, conf);
    pos = get_pos_func(audiotap);
    printf("name %s start %u end %u ends at %d\n", context.block.name, context.block.start, context.block.end, pos);
  };
  free(context.adaptive_tolerances);
}
