/**
 * Copyright 2013-2023 Software Radio Systems Limited
 *
 * This file is part of srsRAN.
 *
 * srsRAN is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsRAN is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include "srsue/hdr/phy/prach.h"
#include "srsran/common/standard_streams.h"
#include "srsran/interfaces/phy_interface_types.h"
#include "srsran/srsran.h"

#define Error(fmt, ...)                                                                                                \
  if (SRSRAN_DEBUG_ENABLED)                                                                                            \
  logger.error(fmt, ##__VA_ARGS__)
#define Warning(fmt, ...)                                                                                              \
  if (SRSRAN_DEBUG_ENABLED)                                                                                            \
  logger.warning(fmt, ##__VA_ARGS__)
#define Info(fmt, ...)                                                                                                 \
  if (SRSRAN_DEBUG_ENABLED)                                                                                            \
  logger.info(fmt, ##__VA_ARGS__)
#define Debug(fmt, ...)                                                                                                \
  if (SRSRAN_DEBUG_ENABLED)                                                                                            \
  logger.debug(fmt, ##__VA_ARGS__)

#define TEST_PRACH_ALL 1

using namespace srsue;

void prach::init(uint32_t max_prb)
{
  std::lock_guard<std::mutex> lock(mutex);

  Debug("PRACH: init() called with max_prb=%d", max_prb);

  if (srsran_cfo_init(&cfo_h, SRSRAN_PRACH_MAX_LEN)) {
    ERROR("PRACH: Error initiating CFO");
    return;
  }

  srsran_cfo_set_tol(&cfo_h, 0);

  signal_buffer = srsran_vec_cf_malloc(SRSRAN_MAX(MAX_LEN_SF * 30720U, SRSRAN_PRACH_MAX_LEN));
  if (!signal_buffer) {
    perror("malloc");
    return;
  }

  if (srsran_prach_init(&prach_obj, srsran_symbol_sz(max_prb))) {
    Error("Initiating PRACH library");
    return;
  }

  Debug("PRACH: prach_obj initialized, MSG1 params in prach_obj: enabled=%d, num_preambles=%d",
        prach_obj.msg1_enabled,
        prach_obj.msg1_num_preambles);

  mem_initiated = true;
}

void prach::stop()
{
  std::lock_guard<std::mutex> lock(mutex);
  if (!mem_initiated) {
    return;
  }

  free(signal_buffer);
  srsran_cfo_free(&cfo_h);
  srsran_prach_free(&prach_obj);
  mem_initiated = false;
}

// set_msg1_params -- configures the struct for msg1_params from phy config passed in
//
void prach::set_msg1_params(bool     enabled,
                            uint32_t num_preambles,
                            uint32_t max_index,
                            float    power,
                            uint32_t ramp_step,
                            float    ramp_db,
                            float    max_ramp_db)
{
  std::lock_guard<std::mutex> lock(mutex);

  Debug("PRACH: Setting MSG1 parameters - enabled=%d, num_preambles=%d, max_index=%d, power=%.2f, "
        "ramp_step=%d, ramp_db=%.2f, max_ramp_db=%.2f",
        enabled,
        num_preambles,
        max_index,
        power,
        ramp_step,
        ramp_db,
        max_ramp_db);

  msg1_enabled       = enabled;
  msg1_num_preambles = num_preambles;
  msg1_max_index     = max_index;
  msg1_power         = power;
  msg1_ramp_step     = ramp_step;
  msg1_ramp_db       = ramp_db;
  msg1_max_ramp_db   = max_ramp_db;

  if (cell_initiated) {
    prach_obj.msg1_enabled            = msg1_enabled;
    prach_obj.msg1_num_preambles      = msg1_num_preambles;
    prach_obj.msg1_max_preamble_index = msg1_max_index;
    prach_obj.msg1_preamble_power     = msg1_power;
    prach_obj.msg1_ramping_step       = msg1_ramp_step;
    prach_obj.msg1_ramping_db         = msg1_ramp_db;
    prach_obj.msg1_max_ramping_db     = msg1_max_ramp_db;

    logger.info("PRACH: msg1 info copied to prach object. Cell already initialized");
  }

  if (enabled) {
    logger.info("MSG1 Attack Mode ENABLED - Config: %d preambles, max_idx=%d, power=%.2f, "
                "ramp=[step:%d, db:%.2f, max:%.2f]",
                num_preambles,
                max_index,
                power,
                ramp_step,
                ramp_db,
                max_ramp_db);
  } else {
    logger.info("MSG1 Attack Mode DISABLED");
  }
}

bool prach::set_cell(srsran_cell_t cell_, srsran_prach_cfg_t prach_cfg)
{
  std::lock_guard<std::mutex> lock(mutex);

  Debug("PRACH: set_cell() called for cell_id=%d", cell_.id);
  Debug("PRACH: Current MSG1 params in class: enabled=%d, num_preambles=%d, max_idx=%d, power=%.2f",
        msg1_enabled,
        msg1_num_preambles,
        msg1_max_index,
        msg1_power);

  if (!mem_initiated) {
    ERROR("PRACH: Error must call init() first");
    return false;
  }

  if (cell.id == cell_.id && cell_initiated && prach_cfg == cfg) {
    Debug("PRACH: Cell already configured, updating msg1 parameters");
    prach_obj.msg1_enabled            = msg1_enabled;
    prach_obj.msg1_num_preambles      = msg1_num_preambles;
    prach_obj.msg1_max_preamble_index = msg1_max_index;
    prach_obj.msg1_preamble_power     = msg1_power;
    prach_obj.msg1_ramping_step       = msg1_ramp_step;
    prach_obj.msg1_ramping_db         = msg1_ramp_db;
    prach_obj.msg1_max_ramping_db     = msg1_max_ramp_db;
    Debug("PRACH: MSG1 parameters updated in prach object");
    return true;
  }

  cell = cell_;
  cfg  = prach_cfg;
  // We must not reset preamble_idx here, MAC might have already called prepare_to_send()

  if (6 + prach_cfg.freq_offset > cell.nof_prb) {
    srsran::console("Error no space for PRACH: frequency offset=%d, N_rb_ul=%d\n", prach_cfg.freq_offset, cell.nof_prb);
    logger.error("Error no space for PRACH: frequency offset=%d, N_rb_ul=%d", prach_cfg.freq_offset, cell.nof_prb);
    return false;
  }

  Info("PRACH: cell.id=%d, configIdx=%d, rootSequence=%d, zeroCorrelationConfig=%d, freqOffset=%d",
       cell.id,
       prach_cfg.config_idx,
       prach_cfg.root_seq_idx,
       prach_cfg.zero_corr_zone,
       prach_cfg.freq_offset);

  if (srsran_prach_set_cfg(&prach_obj, &prach_cfg, cell.nof_prb)) {
    Error("Initiating PRACH library");
    return false;
  }

  len             = prach_obj.N_seq + prach_obj.N_cp;
  transmitted_tti = -1;
  cell_initiated  = true;

  // Set MSG1 parameters in prach_obj
  prach_obj.msg1_enabled            = msg1_enabled;
  prach_obj.msg1_num_preambles      = msg1_num_preambles;
  prach_obj.msg1_max_preamble_index = msg1_max_index;
  prach_obj.msg1_preamble_power     = msg1_power;
  prach_obj.msg1_ramping_step       = msg1_ramp_step;
  prach_obj.msg1_ramping_db         = msg1_ramp_db;
  prach_obj.msg1_max_ramping_db     = msg1_max_ramp_db;

  Debug("PRACH: MSG1 params AFTER copying to prach_obj:");
  Debug("  prach_obj.msg1_enabled = %d", prach_obj.msg1_enabled);
  Debug("  prach_obj.msg1_num_preambles = %d", prach_obj.msg1_num_preambles);
  Debug("  prach_obj.msg1_max_preamble_index = %d", prach_obj.msg1_max_preamble_index);
  Debug("  prach_obj.msg1_preamble_power = %.2f", prach_obj.msg1_preamble_power);
  Debug("  prach_obj.msg1_ramping_step = %d", prach_obj.msg1_ramping_step);
  Debug("  prach_obj.msg1_ramping_db = %.2f", prach_obj.msg1_ramping_db);
  Debug("  prach_obj.msg1_max_ramping_db = %.2f", prach_obj.msg1_max_ramping_db);

  logger.info("Finished setting new PRACH configuration (cell_id=%d).", cell.id);

  return true;
}

// This function generates the PRACH buffer
// Instead of calling the PRACH generator buffer, we call our custom `srsran_prach_gen_all()` function here
//
bool prach::generate_buffer(uint32_t f_idx)
{
  uint32_t freq_offset = cfg.freq_offset;
  if (cell.frame_type == SRSRAN_TDD) {
    freq_offset = srsran_prach_f_ra_tdd(
        cfg.config_idx, cfg.tdd_config.sf_config, (f_idx / 6) * 10, f_idx % 6, cfg.freq_offset, cell.nof_prb);
  }
#ifdef TEST_PRACH_ALL
  if (msg1_enabled) {
    logger.info("Generating MSG1 attack PRACH with custom parameters (freq_offset=%d)", freq_offset);
  } else {
    logger.info(
        "Generating All PRACH preambles to send (freq_offset=%d). Msg1 enabled status: %d", freq_offset, msg1_enabled);
  }

  Debug("PRACH Buffer: Calling srsran_prach_gen_all with freq_offset=%d, f_idx=%d", freq_offset, f_idx);

  if (srsran_prach_gen_all(&prach_obj, freq_offset, signal_buffer)) {
    Error("Generating PRACH preamble %d", preamble_idx);
    return false;
  }
#else
  logger.info("Generating PRACH messages (preamble_idx=%d, freq_offset=%d)", preamble_idx, freq_offset);
  if (srsran_prach_gen(&prach_obj, preamble_idx, freq_offset, signal_buffer)) {
    Error("Generating PRACH preamble %d", preamble_idx);
    return false;
  }
#endif

  Debug("PRACH Buffer: Successfully generated buffer");
  return true;
}

bool prach::prepare_to_send(uint32_t preamble_idx_, int allowed_subframe_, float target_power_dbm_)
{
  std::lock_guard<std::mutex> lock(mutex);
  if (preamble_idx_ >= max_preambles) {
    Error("PRACH: Invalid preamble %d", preamble_idx_);
    return false;
  }

  preamble_idx     = preamble_idx_;
  target_power_dbm = target_power_dbm_;
  allowed_subframe = allowed_subframe_;
  transmitted_tti  = -1;
  Debug("PRACH: prepare to send preamble %d", preamble_idx);
  return true;
}

bool prach::is_pending() const
{
  std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
  if (lock.owns_lock()) {
    return cell_initiated && preamble_idx >= 0 && unsigned(preamble_idx) < max_preambles;
  }
  return false;
}

bool prach::is_ready_to_send(uint32_t current_tti_, uint32_t current_pci)
{
  // Make sure the curernt PCI is the one we configured the PRACH for
  if (is_pending() && current_pci == cell.id) {
    std::lock_guard<std::mutex> lock(mutex);
    // consider the number of subframes the transmission must be anticipated
    uint32_t tti_tx = TTI_TX(current_tti_);
    if (srsran_prach_tti_opportunity(&prach_obj, tti_tx, allowed_subframe)) {
      Debug("PRACH Buffer: Ready to send at tti: %d (now is %d)", tti_tx, current_tti_);
      transmitted_tti = tti_tx;
      return true;
    }
  }
  return false;
}

phy_interface_mac_lte::prach_info_t prach::get_info() const
{
  std::lock_guard<std::mutex>         lock(mutex);
  phy_interface_mac_lte::prach_info_t info = {};

  info.preamble_format = prach_obj.config_idx / 16;
  if (transmitted_tti >= 0) {
    info.tti_ra = (uint32_t)transmitted_tti;
    if (cell.frame_type == SRSRAN_TDD) {
      info.f_id =
          srsran_prach_f_id_tdd(prach_obj.config_idx, prach_obj.tdd_config.sf_config, prach_obj.current_prach_idx);
    }
    info.is_transmitted = true;
  } else {
    info.is_transmitted = false;
  }
  return info;
}

cf_t* prach::generate(float cfo, uint32_t* nof_sf, float* target_power)
{
  std::lock_guard<std::mutex> lock(mutex);
  if (!cell_initiated || preamble_idx < 0 || !nof_sf || unsigned(preamble_idx) >= max_preambles ||
      !srsran_cell_isvalid(&cell) || len >= MAX_LEN_SF * 30720 || len == 0) {
    Error("PRACH: Invalid parameters: cell_initiated=%d, preamble_idx=%d, cell.nof_prb=%d, len=%d",
          cell_initiated,
          preamble_idx,
          cell.nof_prb,
          len);
    return nullptr;
  }

  uint32_t f_idx = 0;
  if (cell.frame_type == SRSRAN_TDD) {
    f_idx = prach_obj.current_prach_idx;
    // For format4, choose odd or even position
    if (prach_obj.config_idx >= 48) {
      f_idx += 6;
    }
    if (f_idx >= max_fs) {
      Error("PRACH Buffer: Invalid f_idx=%d", f_idx);
      f_idx = 0;
    }
  }

  if (!generate_buffer(f_idx)) {
    return nullptr;
  }

  // Correct CFO before transmission
  srsran_cfo_correct(&cfo_h, signal_buffer, signal_buffer, cfo / srsran_symbol_sz(cell.nof_prb));

  // pad guard symbols with zeros
  uint32_t nsf = SRSRAN_CEIL(len, SRSRAN_SF_LEN_PRB(cell.nof_prb));
  srsran_vec_cf_zero(&signal_buffer[len], (nsf * SRSRAN_SF_LEN_PRB(cell.nof_prb) - len));

  *nof_sf = nsf;

  if (target_power) {
    *target_power = target_power_dbm;
  }

  Info("PRACH: Transmitted preamble=%d, tti_tx=%d, CFO=%.2f KHz, nof_sf=%d, target_power=%.1f dBm",
       preamble_idx,
       transmitted_tti,
       cfo * 15,
       nsf,
       target_power_dbm);
  preamble_idx = -1;

  return signal_buffer;
}
