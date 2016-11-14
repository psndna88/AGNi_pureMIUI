#include <linux/mm.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/inet_diag.h>
#include <net/tcp.h>

static int lambda = 20;
module_param(lambda, int, 0644);
MODULE_PARM_DESC(lambda, "degree of sociopathy");

static u32 tcp_sociopath_ssthresh(struct sock *sk) {
  u32 cwnd = tcp_sk(sk)->snd_cwnd;
  return max(cwnd-(cwnd/lambda), 2U);
}

static void tcp_sociopath_cong_avoid(struct sock *sk, u32 ack, u32 in_flight) {
  struct tcp_sock *tp = tcp_sk(sk);
  if (tp->snd_cwnd <= tp->snd_ssthresh) {
    if (tp->snd_cwnd < lambda*10) tp->snd_cwnd = lambda*10;
    tcp_reno_cong_avoid(sk, ack, in_flight);
  } else {
    tcp_reno_cong_avoid(sk, ack, in_flight);
  }
}

static struct tcp_congestion_ops tcp_sociopath = {
  .owner	= THIS_MODULE,
  .name		= "sociopath",

  .ssthresh	= tcp_sociopath_ssthresh,
  .cong_avoid	= tcp_sociopath_cong_avoid
};

static int __init tcp_sociopath_register(void) {
  tcp_register_congestion_control(&tcp_sociopath);
  return 0;
}

static void __exit tcp_sociopath_unregister(void) {
  tcp_unregister_congestion_control(&tcp_sociopath);
}

module_init(tcp_sociopath_register);
module_exit(tcp_sociopath_unregister);

MODULE_AUTHOR("Michael Lucy");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TCP Sociopath");
