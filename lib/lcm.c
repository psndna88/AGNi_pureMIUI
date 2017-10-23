#include <linux/kernel.h>
#include <linux/gcd.h>
#include <linux/export.h>
#include <linux/lcm.h>

/* Lowest common multiple */
unsigned long lcm(unsigned long a, unsigned long b)
{
	if (a && b)
		return (a * b) / gcd(a, b);
	else if (b)
		return b;

	return a;
}
EXPORT_SYMBOL_GPL(lcm);

unsigned long lcm_not_zero(unsigned long a, unsigned long b)
{
	unsigned long l = lcm(a, b);

	if (l)
		return l;

	return (b ? : a);
}
EXPORT_SYMBOL_GPL(lcm_not_zero);
