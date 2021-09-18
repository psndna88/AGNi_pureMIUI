/*
 * TCP CUBIC: Binary Increase Congestion control for TCP v2.3
 * Home page:
 *      http://netsrv.csc.ncsu.edu/twiki/bin/view/Main/BIC
 * This is from the implementation of CUBIC TCP in
 * Sangtae Ha, Injong Rhee and Lisong Xu,
 *  "CUBIC: A New TCP-Friendly High-Speed TCP Variant"
 *  in ACM SIGOPS Operating System Review, July 2008.
 * Available from:
 *  http://netsrv.csc.ncsu.edu/export/cubic_a_new_tcp_2008.pdf
 *
 * CUBIC integrates a new slow start algorithm, called HyStart.
 * The details of HyStart are presented in
 *  Sangtae Ha and Injong Rhee,
 *  "Taming the Elephants: New TCP Slow Start", NCSU TechReport 2008.
 * Available from:
 *  http://netsrv.csc.ncsu.edu/export/hystart_techreport_2008.pdf
 *
 * All testing results are available from:
 * http://netsrv.csc.ncsu.edu/wiki/index.php/TCP_Testing
 *
 * Unless CUBIC is enabled and congestion window is large
 * this behaves the same as the original Reno.
 */
/**
 * C2TCP:Cellular Controlled Delay TCP
 * Home Page:
 * 		https://wp.nyu.edu/c2tcp/
 *
 * This implementation is only a proof-of-concept.
 * A more general version working with different TCP schemes will be released, when I get time! :)
 * Here, C2TCP's functionality is implemented on top of TCP Cubic.
 *
 * Author: Soheil Abbasloo <ab.soheil@nyu.edu>
 *
 * C2TCP is described in detail in:
 *  1- "C2TCP: A Flexible Cellular TCP to Meet Stringent Delay Requirements",
 *  Soheil Abbasloo, et. al., IEEE JSAC'19
 *
 *	2- "Cellular Controlled Delay TCP (C2TCP)",
 *	Soheil Abbasloo, et. al., in IFIP Networking Conference (IFIP Networking) 2018.
 *
 * C2TCP's Repository:
 *		https://github.com/soheil-ab/c2tcp
 *
 * Copyright (C) 2019 Soheil Abbasloo <ab.soheil@nyu.edu>
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/math64.h>
#include <net/tcp.h>
#include <net/codel.h>
#include <linux/inet_diag.h>

#define THR_SCALE 24
#define THR_UNIT (1 << THR_SCALE)

#define BICTCP_BETA_SCALE    1024	/* Scale factor beta calculation
					 * max_cwnd = snd_cwnd * beta
					 */
#define	BICTCP_HZ		10	/* BIC HZ 2^10 = 1024 */

/* Two methods of hybrid slow start */
#define HYSTART_ACK_TRAIN	0x1
#define HYSTART_DELAY		0x2

/* Number of delay samples for detecting the increase of delay */
#define HYSTART_MIN_SAMPLES	8
#define HYSTART_DELAY_MIN	(4000U)	/* 4 ms */
#define HYSTART_DELAY_MAX	(16000U)	/* 16 ms */
#define HYSTART_DELAY_THRESH(x)	clamp(x, HYSTART_DELAY_MIN, HYSTART_DELAY_MAX)

static int fast_convergence __read_mostly = 1;
static int beta __read_mostly = 717;	/* = 717/1024 (BICTCP_BETA_SCALE) */
static int initial_ssthresh __read_mostly;
static int bic_scale __read_mostly = 41;
static int tcp_friendliness __read_mostly = 1;

static int hystart __read_mostly = 1;
static int hystart_detect __read_mostly = HYSTART_ACK_TRAIN | HYSTART_DELAY;
static int hystart_low_window __read_mostly = 16;
static int hystart_ack_delta_us __read_mostly = 2000;

static u32 cube_rtt_scale __read_mostly;
static u32 beta_scale __read_mostly;
static u64 cube_factor __read_mostly;

/*C2TCP: System Params*/
#define TCP_C2TCP_X_SCALE 100
#define TCP_C2TCP_ALPHA_INIT 200		//Initial Alpha = 3 (=TCP_C2TCP_ALPHA_INIT/TCP_C2TCP_X_SCALE)
/*End of C2TP System Params*/

/* Note parameters that are used for precomputing scale factors are read-only */
module_param(fast_convergence, int, 0644);
MODULE_PARM_DESC(fast_convergence, "turn on/off fast convergence");
module_param(beta, int, 0644);
MODULE_PARM_DESC(beta, "beta for multiplicative increase");
module_param(initial_ssthresh, int, 0644);
MODULE_PARM_DESC(initial_ssthresh, "initial value of slow start threshold");
module_param(bic_scale, int, 0444);
MODULE_PARM_DESC(bic_scale, "scale (scaled by 1024) value for bic function (bic_scale/1024)");
module_param(tcp_friendliness, int, 0644);
MODULE_PARM_DESC(tcp_friendliness, "turn on/off tcp friendliness");
module_param(hystart, int, 0644);
MODULE_PARM_DESC(hystart, "turn on/off hybrid slow start algorithm");
module_param(hystart_detect, int, 0644);
MODULE_PARM_DESC(hystart_detect, "hybrid slow start detection mechanisms"
		 " 1: packet-train 2: delay 3: both packet-train and delay");
module_param(hystart_low_window, int, 0644);
MODULE_PARM_DESC(hystart_low_window, "lower bound cwnd for hybrid slow start");
module_param(hystart_ack_delta_us, int, 0644);
MODULE_PARM_DESC(hystart_ack_delta_us, "spacing between ack's indicating train (usecs)");

/* BIC TCP Parameters */
struct bictcp {
	u32	cnt;		/* increase cwnd by 1 after ACKs */
	u32	last_max_cwnd;	/* last maximum snd_cwnd */
	u32	last_cwnd;	/* the last snd_cwnd */
	u32	last_time;	/* time when updated last_cwnd */
	u32	bic_origin_point;/* origin point of bic function */
	u32	bic_K;		/* time to origin point
				   from the beginning of the current epoch */
	u32	delay_min;	/* min delay (usec) */
	u32	epoch_start;	/* beginning of an epoch */
	u32	ack_cnt;	/* number of acks */
	u32	tcp_cwnd;	/* estimated tcp cwnd */
	u16	unused;
	u8	sample_cnt;	/* number of samples to decide curr_rtt */
	u8	found;		/* the exit point is found? */
	u32	round_start;	/* beginning of each round */
	u32	end_seq;	/* end_seq of the round */
	u32	last_ack;	/* last time when the ACK spacing is close */
	u32	curr_rtt;	/* the minimum rtt of current round */
};

static void Newton_step(struct sock * sk)
{
   struct tcp_sock *tp = tcp_sk(sk);
   u32 invsqrt = ((u32)tp->rec_inv_sqrt) << REC_INV_SQRT_SHIFT;
   u32 invsqrt2 = ((u64)invsqrt * invsqrt) >> 32;
   u64 val = (3LL << 32) - ((u64)tp->cnt_rtt * invsqrt2);

   val >>= 2; /* avoid overflow in following multiply */
   val = (val * invsqrt) >> (32 - 2 + 1);

   tp->rec_inv_sqrt = val >> REC_INV_SQRT_SHIFT;
}

static codel_time_t control_law(codel_time_t t,
				      codel_time_t interval,
				      u32 rec_inv_sqrt)
{
	return t + reciprocal_scale(interval, rec_inv_sqrt << REC_INV_SQRT_SHIFT);
}

static void c2tcp_reset(struct sock * sk)
{
   struct tcp_sock *tp = tcp_sk(sk);
   tp->c2tcp_min_urtt=0;
}
static void init_c2tcp(struct sock * sk,int enable)
{
   struct tcp_sock *tp = tcp_sk(sk);
   tp->c2tcp_enable=enable;

   tp->first_above_time=0;
   tp->cnt_rtt=1;
   tp->c2tcp_min_urtt=0;
   tp->c2tcp_cnt=0;
   tp->c2tcp_avg_urtt=0;
   tp->c2tcp_avg_thr=0;
   tp->c2tcp_thr_cnt=0;
   tp->c2tcp_alpha=TCP_C2TCP_ALPHA_INIT;
}

static inline void bictcp_reset(struct bictcp *ca)
{
	memset(ca, 0, offsetof(struct bictcp, unused));
	ca->found = 0;
}

static inline u32 bictcp_clock_us(const struct sock *sk)
{
	return tcp_sk(sk)->tcp_mstamp;
}

static inline void bictcp_hystart_reset(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bictcp *ca = inet_csk_ca(sk);

	ca->round_start = ca->last_ack = bictcp_clock_us(sk);
	ca->end_seq = tp->snd_nxt;
	ca->curr_rtt = ~0U;
	ca->sample_cnt = 0;
}

static void cubictcp_init(struct sock *sk)
{
	struct bictcp *ca = inet_csk_ca(sk);

	bictcp_reset(ca);

	if (hystart)
		bictcp_hystart_reset(sk);

	if (!hystart && initial_ssthresh)
		tcp_sk(sk)->snd_ssthresh = initial_ssthresh;

	/*C2TCP*/
	init_c2tcp(sk,1);
}

/*C2TCP-Functionality */
static void c2tcp_pkts_acked(struct sock *sk,u32 cnt,s32 rtt_us)
{
   const struct inet_connection_sock *icsk = inet_csk(sk);
   struct tcp_sock *tp = tcp_sk(sk);
   u32 tmp,tmp2,tmp_min_msrtt;
   codel_time_t c2tcp_setpoint;
   codel_time_t c2tcp_interval;
   codel_time_t now=codel_get_time();
   codel_time_t c2tcp_rtt=MS2TIME(rtt_us/USEC_PER_MSEC);
   codel_time_t c2tcp_next_time=MS2TIME(tp->next_time/USEC_PER_MSEC);

   if(tp->c2tcp_min_urtt==0 || tp->c2tcp_min_urtt>rtt_us)
		tp->c2tcp_min_urtt=rtt_us;
   if(rtt_us>0)
   {
	    u64 tmp_avg=0;
        u64 tmp2_avg=0;
		tmp_avg=(tp->c2tcp_cnt)*tp->c2tcp_avg_urtt+rtt_us;
		tp->c2tcp_cnt++;
		tmp2_avg=tp->c2tcp_cnt;
		tmp2_avg=tmp_avg/tp->c2tcp_cnt;
		tp->c2tcp_avg_urtt=(u32)(tmp2_avg);
   }

   tmp_min_msrtt=(tp->c2tcp_min_urtt/USEC_PER_MSEC);
   tmp=(tmp_min_msrtt*tp->c2tcp_alpha)/TCP_C2TCP_X_SCALE;
   c2tcp_setpoint=MS2TIME(tmp);
   tmp2=(tmp_min_msrtt*tp->c2tcp_alpha)/TCP_C2TCP_X_SCALE;
   c2tcp_interval=MS2TIME(tmp2);

   if(codel_time_after_eq(c2tcp_setpoint,c2tcp_rtt))
   {
       tp->first_above_time=0;
       tp->cnt_rtt=1;
       //tp->first_time=0;
//     bictcp_update(ca, tp->snd_cwnd);
       tmp2=(rtt_us/USEC_PER_MSEC);
       if (tmp2==0)
           tmp2++;
       tmp=(tmp)/(tmp2);

       tp->snd_cwnd_cnt += tmp;
       if (tp->snd_cwnd_cnt >= tp->snd_cwnd) {
    	   u32 delta = tp->snd_cwnd_cnt / tp->snd_cwnd;
    	   tp->snd_cwnd_cnt -= delta * tp->snd_cwnd;
    	   tp->snd_cwnd += delta;
           tp->snd_cwnd = min(tp->snd_cwnd, tp->snd_cwnd_clamp);
       }
  }
  else if (tp->first_above_time==0)
  {
      tp->first_above_time=codel_time_to_us((now+c2tcp_interval));
      tp->next_time=tp->first_above_time;
       //tp->first_time=1;
       tp->cnt_rtt=1;
       tp->rec_inv_sqrt= ~0U >> REC_INV_SQRT_SHIFT;
       Newton_step(sk);
   }
   else if (codel_time_after(now,c2tcp_next_time))
   {
       c2tcp_next_time=control_law(now,c2tcp_interval,tp->rec_inv_sqrt);

       tp->next_time=codel_time_to_us(c2tcp_next_time);
       tp->cnt_rtt++;
       Newton_step(sk);

       tp->prior_ssthresh = tcp_current_ssthresh(sk);
       tp->snd_ssthresh = icsk->icsk_ca_ops->ssthresh(sk);
       tp->snd_cwnd       = 1;
       tp->snd_cwnd_cnt   = 0;
//     printk(KERN_INFO "c2tcp triggerd! cnt:%d, changing ssthresh from %d to %d\n",tp->cnt_rtt-1,tp->prior_ssthresh,tp->snd_ssthresh);
   }
}

static void cubictcp_cwnd_event(struct sock *sk, enum tcp_ca_event event)
{
	if (event == CA_EVENT_TX_START) {
		struct bictcp *ca = inet_csk_ca(sk);
		u32 now = tcp_jiffies32;
		s32 delta;

		delta = now - tcp_sk(sk)->lsndtime;

		/* We were application limited (idle) for a while.
		 * Shift epoch_start to keep cwnd growth to cubic curve.
		 */
		if (ca->epoch_start && delta > 0) {
			ca->epoch_start += delta;
			if (after(ca->epoch_start, now))
				ca->epoch_start = now;
		}
		return;
	}
}

/* calculate the cubic root of x using a table lookup followed by one
 * Newton-Raphson iteration.
 * Avg err ~= 0.195%
 */
static u32 cubic_root(u64 a)
{
	u32 x, b, shift;
	/*
	 * cbrt(x) MSB values for x MSB values in [0..63].
	 * Precomputed then refined by hand - Willy Tarreau
	 *
	 * For x in [0..63],
	 *   v = cbrt(x << 18) - 1
	 *   cbrt(x) = (v[x] + 10) >> 6
	 */
	static const u8 v[] = {
		/* 0x00 */    0,   54,   54,   54,  118,  118,  118,  118,
		/* 0x08 */  123,  129,  134,  138,  143,  147,  151,  156,
		/* 0x10 */  157,  161,  164,  168,  170,  173,  176,  179,
		/* 0x18 */  181,  185,  187,  190,  192,  194,  197,  199,
		/* 0x20 */  200,  202,  204,  206,  209,  211,  213,  215,
		/* 0x28 */  217,  219,  221,  222,  224,  225,  227,  229,
		/* 0x30 */  231,  232,  234,  236,  237,  239,  240,  242,
		/* 0x38 */  244,  245,  246,  248,  250,  251,  252,  254,
	};

	b = fls64(a);
	if (b < 7) {
		/* a in [0..63] */
		return ((u32)v[(u32)a] + 35) >> 6;
	}

	b = ((b * 84) >> 8) - 1;
	shift = (a >> (b * 3));

	x = ((u32)(((u32)v[shift] + 10) << b)) >> 6;

	/*
	 * Newton-Raphson iteration
	 *                         2
	 * x    = ( 2 * x  +  a / x  ) / 3
	 *  k+1          k         k
	 */
	x = (2 * x + (u32)div64_u64(a, (u64)x * (u64)(x - 1)));
	x = ((x * 341) >> 10);
	return x;
}

/*
 * Compute congestion window to use.
 */
static inline void bictcp_update(struct bictcp *ca, u32 cwnd, u32 acked)
{
	u32 delta, bic_target, max_cnt;
	u64 offs, t;

	ca->ack_cnt += acked;	/* count the number of ACKed packets */

	if (ca->last_cwnd == cwnd &&
	    (s32)(tcp_jiffies32 - ca->last_time) <= HZ / 32)
		return;

	/* The CUBIC function can update ca->cnt at most once per jiffy.
	 * On all cwnd reduction events, ca->epoch_start is set to 0,
	 * which will force a recalculation of ca->cnt.
	 */
	if (ca->epoch_start && tcp_jiffies32 == ca->last_time)
		goto tcp_friendliness;

	ca->last_cwnd = cwnd;
	ca->last_time = tcp_jiffies32;

	if (ca->epoch_start == 0) {
		ca->epoch_start = tcp_jiffies32;	/* record beginning */
		ca->ack_cnt = acked;			/* start counting */
		ca->tcp_cwnd = cwnd;			/* syn with cubic */

		if (ca->last_max_cwnd <= cwnd) {
			ca->bic_K = 0;
			ca->bic_origin_point = cwnd;
		} else {
			/* Compute new K based on
			 * (wmax-cwnd) * (srtt>>3 / HZ) / c * 2^(3*bictcp_HZ)
			 */
			ca->bic_K = cubic_root(cube_factor
					       * (ca->last_max_cwnd - cwnd));
			ca->bic_origin_point = ca->last_max_cwnd;
		}
	}

	/* cubic function - calc*/
	/* calculate c * time^3 / rtt,
	 *  while considering overflow in calculation of time^3
	 * (so time^3 is done by using 64 bit)
	 * and without the support of division of 64bit numbers
	 * (so all divisions are done by using 32 bit)
	 *  also NOTE the unit of those veriables
	 *	  time  = (t - K) / 2^bictcp_HZ
	 *	  c = bic_scale >> 10
	 * rtt  = (srtt >> 3) / HZ
	 * !!! The following code does not have overflow problems,
	 * if the cwnd < 1 million packets !!!
	 */

	t = (s32)(tcp_jiffies32 - ca->epoch_start);
	t += usecs_to_jiffies(ca->delay_min);
	/* change the unit from HZ to bictcp_HZ */
	t <<= BICTCP_HZ;
	do_div(t, HZ);

	if (t < ca->bic_K)		/* t - K */
		offs = ca->bic_K - t;
	else
		offs = t - ca->bic_K;

	/* c/rtt * (t-K)^3 */
	delta = (cube_rtt_scale * offs * offs * offs) >> (10+3*BICTCP_HZ);
	if (t < ca->bic_K)                            /* below origin*/
		bic_target = ca->bic_origin_point - delta;
	else                                          /* above origin*/
		bic_target = ca->bic_origin_point + delta;

	/* cubic function - calc bictcp_cnt*/
	if (bic_target > cwnd) {
		ca->cnt = cwnd / (bic_target - cwnd);
	} else {
		ca->cnt = 100 * cwnd;              /* very small increment*/
	}

	/*
	 * The initial growth of cubic function may be too conservative
	 * when the available bandwidth is still unknown.
	 */
	if (ca->last_max_cwnd == 0 && ca->cnt > 20)
		ca->cnt = 20;	/* increase cwnd 5% per RTT */

tcp_friendliness:
	/* TCP Friendly */
	if (tcp_friendliness) {
		u32 scale = beta_scale;

		delta = (cwnd * scale) >> 3;
		while (ca->ack_cnt > delta) {		/* update tcp cwnd */
			ca->ack_cnt -= delta;
			ca->tcp_cwnd++;
		}

		if (ca->tcp_cwnd > cwnd) {	/* if bic is slower than tcp */
			delta = ca->tcp_cwnd - cwnd;
			max_cnt = cwnd / delta;
			if (ca->cnt > max_cnt)
				ca->cnt = max_cnt;
		}
	}

	/* The maximum rate of cwnd increase CUBIC allows is 1 packet per
	 * 2 packets ACKed, meaning cwnd grows at 1.5x per RTT.
	 */
	ca->cnt = max(ca->cnt, 2U);
}

static void cubictcp_cong_avoid(struct sock *sk, u32 ack, u32 acked)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bictcp *ca = inet_csk_ca(sk);

	if (!tcp_is_cwnd_limited(sk))
		return;

	if (tcp_in_slow_start(tp)) {
		if (hystart && after(ack, ca->end_seq))
			bictcp_hystart_reset(sk);
		acked = tcp_slow_start(tp, acked);
		if (!acked)
			return;
	}
	bictcp_update(ca, tp->snd_cwnd, acked);
	tcp_cong_avoid_ai(tp, ca->cnt, acked);
}

static u32 cubictcp_recalc_ssthresh(struct sock *sk)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	struct bictcp *ca = inet_csk_ca(sk);

	ca->epoch_start = 0;	/* end of epoch */

	/* Wmax and fast convergence */
	if (tp->snd_cwnd < ca->last_max_cwnd && fast_convergence)
		ca->last_max_cwnd = (tp->snd_cwnd * (BICTCP_BETA_SCALE + beta))
			/ (2 * BICTCP_BETA_SCALE);
	else
		ca->last_max_cwnd = tp->snd_cwnd;

	return max((tp->snd_cwnd * beta) / BICTCP_BETA_SCALE, 2U);
}

static void cubictcp_state(struct sock *sk, u8 new_state)
{
	if (new_state == TCP_CA_Loss) {
		bictcp_reset(inet_csk_ca(sk));
		bictcp_hystart_reset(sk);
	}
}

/* Account for TSO/GRO delays.
 * Otherwise short RTT flows could get too small ssthresh, since during
 * slow start we begin with small TSO packets and ca->delay_min would
 * not account for long aggregation delay when TSO packets get bigger.
 * Ideally even with a very small RTT we would like to have at least one
 * TSO packet being sent and received by GRO, and another one in qdisc layer.
 * We apply another 100% factor because @rate is doubled at this point.
 * We cap the cushion to 1ms.
 */
static u32 hystart_ack_delay(struct sock *sk)
{
	unsigned long rate;

	rate = READ_ONCE(sk->sk_pacing_rate);
	if (!rate)
		return 0;
	return min_t(u64, USEC_PER_MSEC,
		     div64_ul((u64)GSO_MAX_SIZE * 4 * USEC_PER_SEC, rate));
}

static void hystart_update(struct sock *sk, u32 delay)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bictcp *ca = inet_csk_ca(sk);
	u32 threshold;

	if (hystart_detect & HYSTART_ACK_TRAIN) {
		u32 now = bictcp_clock_us(sk);

		/* first detection parameter - ack-train detection */
		if ((s32)(now - ca->last_ack) <= hystart_ack_delta_us) {
			ca->last_ack = now;

			threshold = ca->delay_min + hystart_ack_delay(sk);

			/* Hystart ack train triggers if we get ack past
			 * ca->delay_min/2.
			 * Pacing might have delayed packets up to RTT/2
			 * during slow start.
			 */
			if (sk->sk_pacing_status == SK_PACING_NONE)
				threshold >>= 1;

			if ((s32)(now - ca->round_start) > threshold) {
				ca->found = 1;
				pr_debug("hystart_ack_train (%u > %u) delay_min %u (+ ack_delay %u) cwnd %u\n",
					 now - ca->round_start, threshold,
					 ca->delay_min, hystart_ack_delay(sk), tp->snd_cwnd);
				NET_INC_STATS(sock_net(sk),
					      LINUX_MIB_TCPHYSTARTTRAINDETECT);
				NET_ADD_STATS(sock_net(sk),
					      LINUX_MIB_TCPHYSTARTTRAINCWND,
					      tp->snd_cwnd);
				tp->snd_ssthresh = tp->snd_cwnd;
			}
		}
	}

	if (hystart_detect & HYSTART_DELAY) {
		/* obtain the minimum delay of more than sampling packets */
		if (ca->curr_rtt > delay)
			ca->curr_rtt = delay;
		if (ca->sample_cnt < HYSTART_MIN_SAMPLES) {
			if (ca->curr_rtt > delay)
				ca->curr_rtt = delay;

			ca->sample_cnt++;
		} else {
			if (ca->curr_rtt > ca->delay_min +
			    HYSTART_DELAY_THRESH(ca->delay_min >> 3)) {
				ca->found = 1;
				NET_INC_STATS(sock_net(sk),
					      LINUX_MIB_TCPHYSTARTDELAYDETECT);
				NET_ADD_STATS(sock_net(sk),
					      LINUX_MIB_TCPHYSTARTDELAYCWND,
					      tp->snd_cwnd);
				tp->snd_ssthresh = tp->snd_cwnd;
			}
		}
	}
}

static void cubictcp_acked(struct sock *sk, const struct ack_sample *sample)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	struct bictcp *ca = inet_csk_ca(sk);
	u32 delay;

	/* Some calls are for duplicates without timetamps */
	if (sample->rtt_us < 0)
		return;

	/* Discard delay samples right after fast recovery */
	if (ca->epoch_start && (s32)(tcp_jiffies32 - ca->epoch_start) < HZ)
		return;

	delay = sample->rtt_us;
	if (delay == 0)
		delay = 1;

	/* first time call or link delay decreases */
	if (ca->delay_min == 0 || ca->delay_min > delay)
		ca->delay_min = delay;

	/* hystart triggers when cwnd is larger than some threshold */
	if (!ca->found && tcp_in_slow_start(tp) && hystart &&
	    tp->snd_cwnd >= hystart_low_window)
		hystart_update(sk, delay);

	/* C2TCP Functionality */
	c2tcp_pkts_acked(sk, sample->pkts_acked, sample->rtt_us);
}

static void natcp_update_by_app(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	printk("natcp_update_by_app:snd_cwnd:%d cwnd_clamp:%d \n",
			tp->snd_cwnd,tp->snd_cwnd_clamp);
	tp->snd_cwnd =min(tp->snd_cwnd,tp->snd_cwnd_clamp);
}

static size_t c2tcp_get_info(struct sock *sk, u32 ext, int *attr,
			   union tcp_cc_info *info)
{
	struct tcp_sock *tp = tcp_sk(sk);
	if (ext & (1 << (INET_DIAG_C2TCPINFO - 1)) ) {
		memset(&info->c2tcp, 0, sizeof(info->c2tcp));
		info->c2tcp.c2tcp_avg_urtt		= tp->c2tcp_avg_urtt;
		info->c2tcp.c2tcp_min_rtt		= tp->c2tcp_min_urtt;
		info->c2tcp.c2tcp_cnt			= tp->c2tcp_cnt;
		info->c2tcp.c2tcp_avg_thr		= tp->c2tcp_avg_thr * tp->mss_cache * USEC_PER_SEC >> THR_SCALE;
		info->c2tcp.c2tcp_thr_cnt		= tp->c2tcp_thr_cnt;
		*attr = INET_DIAG_C2TCPINFO;
		tp->c2tcp_cnt=0;
		tp->c2tcp_avg_urtt=0;
		tp->c2tcp_thr_cnt=0;
		tp->c2tcp_avg_thr=0;
		return sizeof(info->c2tcp);
	}

	return 0;
}

/* Estimate the bandwidth based on how fast packets are delivered */
static void c2tcp_get_rate_sample(struct sock *sk, const struct rate_sample *rs)
{
	struct tcp_sock *tp = tcp_sk(sk);
	u64 bw;
	if (rs->delivered < 0 || rs->interval_us <= 0)
		return; /* Not a valid observation */

	/* Divide delivered by the interval to find a (lower bound) bottleneck
	 * bandwidth sample. Delivered is in packets and interval_us in uS and
	 * ratio will be <<1 for most connections. So delivered is first scaled.
	 */
	bw = (u64)rs->delivered * THR_UNIT;
	do_div(bw, rs->interval_us);
	tp->c2tcp_avg_thr=tp->c2tcp_avg_thr*tp->c2tcp_thr_cnt+bw;
	tp->c2tcp_thr_cnt=tp->c2tcp_thr_cnt+1;
	do_div(tp->c2tcp_avg_thr,tp->c2tcp_thr_cnt);
}

static void get_rate_sample(struct sock *sk, const struct rate_sample *rs)
{
	c2tcp_get_rate_sample(sk,rs);
}
//End

static void enable_c2tcp(struct sock *sk,int val)
{
	struct tcp_sock *tp = tcp_sk(sk);
	init_c2tcp(sk,val);
}

static struct tcp_congestion_ops c2tcp __read_mostly = {
	.init		= cubictcp_init,
	.ssthresh	= cubictcp_recalc_ssthresh,
	.cong_avoid	= cubictcp_cong_avoid,
	.set_state	= cubictcp_state,
	.undo_cwnd	= tcp_reno_undo_cwnd,
	.cwnd_event	= cubictcp_cwnd_event,
	//S.A: To support NATCP
	.update_by_app	= natcp_update_by_app,
	.get_rate_sample = get_rate_sample,
	.pkts_acked     = cubictcp_acked,
	.owner		= THIS_MODULE,
	.get_info	= c2tcp_get_info,
	.enable_c2tcp = enable_c2tcp,
	.name		= "c2tcp",
};

static int __init c2tcp_register(void)
{
	BUILD_BUG_ON(sizeof(struct bictcp) > ICSK_CA_PRIV_SIZE);

	/* Precompute a bunch of the scaling factors that are used per-packet
	 * based on SRTT of 100ms
	 */

	beta_scale = 8*(BICTCP_BETA_SCALE+beta) / 3
		/ (BICTCP_BETA_SCALE - beta);

	cube_rtt_scale = (bic_scale * 10);	/* 1024*c/rtt */

	/* calculate the "K" for (wmax-cwnd) = c/rtt * K^3
	 *  so K = cubic_root( (wmax-cwnd)*rtt/c )
	 * the unit of K is bictcp_HZ=2^10, not HZ
	 *
	 *  c = bic_scale >> 10
	 *  rtt = 100ms
	 *
	 * the following code has been designed and tested for
	 * cwnd < 1 million packets
	 * RTT < 100 seconds
	 * HZ < 1,000,00  (corresponding to 10 nano-second)
	 */

	/* 1/c * 2^2*bictcp_HZ * srtt */
	cube_factor = 1ull << (10+3*BICTCP_HZ); /* 2^40 */

	/* divide by bic_scale and by constant Srtt (100ms) */
	do_div(cube_factor, bic_scale * 10);

	return tcp_register_congestion_control(&c2tcp);
}

static void __exit c2tcp_unregister(void)
{
	tcp_unregister_congestion_control(&c2tcp);
}

module_init(c2tcp_register);
module_exit(c2tcp_unregister);

MODULE_AUTHOR("Soheil Abbasloo");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CELLULAR CONTROLLED TCP");
MODULE_VERSION("1.0");
