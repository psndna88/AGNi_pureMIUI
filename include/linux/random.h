/*
 * include/linux/random.h
 *
 * Include file for the random number generator.
 */
#ifndef _LINUX_RANDOM_H
#define _LINUX_RANDOM_H

#include <linux/list.h>
#include <uapi/linux/random.h>

struct random_ready_callback {
	struct list_head list;
	void (*func)(struct random_ready_callback *rdy);
	struct module *owner;
};

extern void add_device_randomness(const void *, unsigned int);
extern void add_input_randomness(unsigned int type, unsigned int code,
				 unsigned int value);
extern void add_interrupt_randomness(int irq, int irq_flags);

extern void get_random_bytes(void *buf, int nbytes);
extern int add_random_ready_callback(struct random_ready_callback *rdy);
extern void del_random_ready_callback(struct random_ready_callback *rdy);
extern void get_random_bytes_arch(void *buf, int nbytes);
void generate_random_uuid(unsigned char uuid_out[16]);
extern int random_int_secret_init(void);

#ifndef MODULE
extern const struct file_operations random_fops, urandom_fops;
#endif

unsigned int get_random_int(void);
unsigned long get_random_long(void);
unsigned long randomize_range(unsigned long start, unsigned long end, unsigned long len);

u32 prandom_u32(void);
void prandom_bytes(void *buf, int nbytes);
void prandom_seed(u32 seed);
void prandom_reseed_late(void);

u32 prandom_u32_state(struct rnd_state *);
void prandom_bytes_state(struct rnd_state *state, void *buf, int nbytes);

/*
 * Handle minimum values for seeds
 */
static inline u32 __seed(u32 x, u32 m)
{
	return (x < m) ? x + m : x;
}

/**
 * prandom_seed_state - set seed for prandom_u32_state().
 * @state: pointer to state structure to receive the seed.
 * @seed: arbitrary 64-bit value to use as a seed.
 */
static inline void prandom_seed_state(struct rnd_state *state, u64 seed)
{
	u32 i = (seed >> 32) ^ (seed << 10) ^ seed;

	state->s1 = __seed(i, 2);
	state->s2 = __seed(i, 8);
	state->s3 = __seed(i, 16);
}

#ifdef CONFIG_ARCH_RANDOM
# include <asm/archrandom.h>
#else
static inline int arch_get_random_long(unsigned long *v)
{
	return 0;
}
static inline int arch_get_random_int(unsigned int *v)
{
	return 0;
}
#endif

/* Pseudo random number generator from numerical recipes. */
static inline u32 next_pseudo_random32(u32 seed)
{
	return seed * 1664525 + 1013904223;
}

#endif /* _LINUX_RANDOM_H */
