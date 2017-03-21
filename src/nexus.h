#ifndef ERPC_NEXUS_H
#define ERPC_NEXUS_H

#include <signal.h>
#include <unistd.h>

#include "common.h"
#include "session.h"
#include "session_mgmt_types.h"
using namespace std;

namespace ERpc {

/// A work item submitted to a background thread. Also a work completion.
struct bg_work_item_t {
  uint8_t app_tid;  ///< TID of the Rpc that submitted this request
  Session *session;
  Session::sslot_t *sslot;
};

/// A hook created by the per-thread Rpc, and shared with the per-process Nexus.
/// All accesses to this hook must be done with @session_mgmt_mutex locked.
class NexusHook {
 public:
  NexusHook(uint8_t app_tid) : app_tid(app_tid) {}

  const uint8_t app_tid;  ///< App TID of the RPC that created this hook

  MtList<SessionMgmtPkt *> sm_pkt_list;   ///< Session management packet list
  MtList<bg_work_item_t *> bg_resp_list;  ///< Background thread response list

  /// List of background thread request submission list, filled in by the Nexus
  MtList<bg_work_item_t *> *bg_req_list_arr[kMaxBgThreads];
};

/// Background thread context
class BgThreadCtx {
 public:
  /// A switch used by the Nexus to turn off background threads
  volatile bool *bg_kill_switch;

  size_t bg_thread_id;                   ///< ID of the background thread
  MtList<bg_work_item_t *> bg_req_list;  ///< The list to submit requests to
  NexusHook **reg_hooks_arr;
};

class Nexus {
  static constexpr double kMaxUdpDropProb = .95;  ///< Max UDP packet drop prob
 public:
  /**
   * @brief Create the one-per-process Nexus object.
   *
   * @param mgmt_udp_port The UDP port used by all Nexus-es in the cluster to
   * listen for session management packets
   *
   * @param num_bg_threads The number of background RPC request processing
   * threads to launch
   *
   * @param udp_drop_prob The probability that a session management packet
   * will be dropped. This is useful for testing session management packet
   * retransmission.
   *
   * @throw runtime_error if Nexus creation fails.
   */
  Nexus(uint16_t mgmt_udp_port, size_t num_bg_threads = 0,
        double udp_drop_prob = 0.0);

  ~Nexus();

  /**
   * @brief Check if a hook with app TID = \p app_tid exists in this Nexus. The
   * caller must not hold the Nexus lock before calling this.
   */
  bool app_tid_exists(uint8_t app_tid);

  /// Register a previously unregistered session management hook
  void register_hook(NexusHook *hook);

  /// Unregister a previously registered session management hook
  void unregister_hook(NexusHook *hook);

  void install_sigio_handler();
  void session_mgnt_handler();

  /**
   * @brief Register application-defined operations for a request type. This
   * must be done before any Rpc registers a hook with the Nexus.
   *
   * @return 0 on success, errno on failure.
   */
  int register_ops(uint8_t req_type, Ops app_ops);

  /**
   * @brief Copy the hostname of this machine to \p hostname. \p hostname must
   * have space for kMaxHostnameLen characters.
   *
   * @return 0 on success, -1 on error.
   */
  static int get_hostname(char *_hostname) {
    assert(_hostname != nullptr);

    int ret = gethostname(_hostname, kMaxHostnameLen);
    return ret;
  }

  /// The function executed by background RPC-processing threads
  static void bg_thread_func(BgThreadCtx *bg_thread_ctx) {
    volatile bool *bg_kill_switch = bg_thread_ctx->bg_kill_switch;
    while (*bg_kill_switch != true) {
      usleep(200000);
    }
  }

  /// Read-mostly members exposed to Rpc threads
  const udp_config_t udp_config;  ///< UDP port and packet drop probability
  const double freq_ghz;          ///< Rdtsc frequncy
  const std::string hostname;     ///< The local host
  const size_t num_bg_threads;    ///< Background threads to process Rpc reqs

  const uint8_t pad[64] = {0};

  /// The ground truth for registered Ops
  std::array<Ops, kMaxReqTypes> ops_arr;

  /// Ops registration is disallowed after any Rpc registers and gets a copy
  /// of ops_arr
  bool ops_registration_allowed = true;

  /// Read-write members exposed to Rpc threads
  std::mutex nexus_lock;  ///< Lock for concurrent access to this Nexus
  NexusHook *reg_hooks_arr[kMaxAppTid + 1] = {nullptr};  ///< Rpc-Nexus hooks

 private:
  int sm_sock_fd;  ///< File descriptor of the session management UDP socket

  // Background threads
  volatile bool bg_kill_switch;  ///< A switch to turn off background threads
  std::thread bg_thread_arr[kMaxBgThreads];      ///< The background threads
  BgThreadCtx bg_thread_ctx_arr[kMaxBgThreads];  ///< Background thread context

  /// Return the frequency of rdtsc in GHz
  static double get_freq_ghz();

  /// Return the hostname of this machine
  static std::string get_hostname();
};

static Nexus *nexus_object; /* The one per-process Nexus object */

/**
 * @brief The static signal handler, which executes the actual signal handler
 * with the one Nexus object.
 */
static void sigio_handler(int sig_num) {
  assert(sig_num == SIGIO);
  _unused(sig_num);
  nexus_object->session_mgnt_handler();
}

}  // End ERpc

#endif  // ERPC_RPC_H
