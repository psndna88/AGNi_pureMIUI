/* Monitoring and Action Enforcer Blocks of DeepCC and Orca
 *
 * Author: Soheil Abbasloo <ab.soheil@nyu.edu>
 *
 * Generating useful states/information to be used by the Deep Reinforcement Learning Block.
 *
 * Orca is described in detail in:
 *   "Classic Meets Modern: A Pragmatic Learning-Based Congestion Control for the Internet",
 *   Soheil Abbasloo, Chen-Yu Yen, H. J. Chao. In Proc ACM SIGCOMM'20
 *
 * Orca's Repository:
 *		https://github.com/soheil-ab/orca
 *
 * Copyright (C) 2020 Soheil Abbasloo <ab.soheil@nyu.edu>
 */

#include <net/tcp.h>
#include <linux/inet_diag.h>

/*
 * Samplings required for DeepCC/Orca
 */

#define THR_SCALE_DEEPCC 24
#define THR_UNIT_DEEPCC (1 << THR_SCALE_DEEPCC)

void deepcc_init(struct sock * sk)
{
   struct tcp_sock *tp = tcp_sk(sk);
   tp->deepcc_api.min_urtt=0;
   tp->deepcc_api.cnt=0;
   tp->deepcc_api.avg_urtt=0;
   tp->deepcc_api.avg_thr=0;
   tp->deepcc_api.thr_cnt=0;
   tp->deepcc_api.pre_lost=0;
}

size_t deepcc_get_info(struct sock *sk, u32 ext, int *attr,union tcp_cc_info *info)
{
	if (ext & (1 << (INET_DIAG_DEEPCCINFO - 1)) ) {
		struct tcp_sock *tp = tcp_sk(sk);
		memset(&info->deepcc, 0, sizeof(info->deepcc));
		info->deepcc.avg_urtt		= tp->deepcc_api.avg_urtt;
		info->deepcc.min_rtt		= tp->deepcc_api.min_urtt;
		info->deepcc.cnt			= tp->deepcc_api.cnt;
		info->deepcc.avg_thr		= tp->deepcc_api.avg_thr * tp->mss_cache * USEC_PER_SEC >> THR_SCALE_DEEPCC;
		info->deepcc.thr_cnt		= tp->deepcc_api.thr_cnt;
		info->deepcc.cwnd			= tp->snd_cwnd;
		info->deepcc.pacing_rate	= sk->sk_pacing_rate;
		info->deepcc.lost_bytes		= (tp->lost - tp->deepcc_api.pre_lost)* tp->mss_cache;
		info->deepcc.srtt_us		= tp->srtt_us;				/* smoothed round trip time << 3 in usecs */
		info->deepcc.snd_ssthresh	= tp->snd_ssthresh;			/* Slow start size threshold		*/
		info->deepcc.packets_out	= tp->packets_out;			/* Packets which are "in flight"	*/
		info->deepcc.retrans_out	= tp->retrans_out;			/* Retransmitted packets out		*/
		info->deepcc.max_packets_out= tp->max_packets_out;  	/* max packets_out in last window */
		info->deepcc.mss_cache		= tp->mss_cache;		  	/* max packets_out in last window */

		*attr = INET_DIAG_DEEPCCINFO;
		tp->deepcc_api.cnt=0;
		tp->deepcc_api.avg_urtt=0;
		tp->deepcc_api.thr_cnt=0;
		tp->deepcc_api.avg_thr=0;
		tp->deepcc_api.pre_lost=tp->lost;

		return sizeof(info->deepcc);
	}
	return 0;
}

void deepcc_update_pacing_rate(struct sock *sk)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	u64 rate;
	cmpxchg(&sk->sk_pacing_status, SK_PACING_NONE, SK_PACING_NEEDED);

	rate = tcp_mss_to_mtu(sk, tcp_sk(sk)->mss_cache); //

	rate *= USEC_PER_SEC;

	rate *= max(tp->snd_cwnd, tp->packets_out);

	if (likely(tp->srtt_us>>3))
		do_div(rate, tp->srtt_us>>3);

	/* ACCESS_ONCE() is needed because sch_fq fetches sk_pacing_rate
	 * without any lock. We want to make sure compiler wont store
	 * intermediate values in this location.
	 */
	ACCESS_ONCE(sk->sk_pacing_rate) = min_t(u64, rate,sk->sk_max_pacing_rate);
}

void deepcc_update_cwnd(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	tp->snd_cwnd = max(tp->snd_cwnd,tp->cwnd_min);
	tp->snd_cwnd =min(tp->snd_cwnd,tp->snd_cwnd_clamp);
	deepcc_update_pacing_rate(sk);
}

void deepcc_get_rate_sample(struct sock *sk, const struct rate_sample *rs)
{
	struct tcp_sock *tp = tcp_sk(sk);
	u64 bw;
	if (rs->delivered < 0 || rs->interval_us <= 0)
		return; /* Not a valid observation */

	bw = (u64)rs->delivered * THR_UNIT_DEEPCC;
	do_div(bw, rs->interval_us);
	tp->deepcc_api.avg_thr=tp->deepcc_api.avg_thr*tp->deepcc_api.thr_cnt+bw;
	tp->deepcc_api.thr_cnt=tp->deepcc_api.thr_cnt+1;
	do_div(tp->deepcc_api.avg_thr,tp->deepcc_api.thr_cnt);
}

void deepcc_pkts_acked(struct sock *sk, const struct ack_sample *sample)
{
	struct tcp_sock *tp = tcp_sk(sk);

	/* Some calls are for duplicates without timetamps */
	if (sample->rtt_us < 0)
		return;

	if(tp->deepcc_api.min_urtt==0 || tp->deepcc_api.min_urtt>sample->rtt_us)
			tp->deepcc_api.min_urtt=sample->rtt_us;
	if(sample->rtt_us>0)
	{
		u64 tmp_avg=0;
		u64 tmp2_avg=0;
		tmp_avg=(tp->deepcc_api.cnt)*tp->deepcc_api.avg_urtt+sample->rtt_us;
		tp->deepcc_api.cnt++;
		tmp2_avg=tp->deepcc_api.cnt;
		tmp2_avg=tmp_avg/tp->deepcc_api.cnt;
		tp->deepcc_api.avg_urtt=(u32)(tmp2_avg);
	}
}
//END
