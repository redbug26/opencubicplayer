#ifndef PLAYYM_YMTYPE_H
#define PLAYYM_YMTYPE_H 1

int __attribute__((visibility ("internal"))) ym_type_init (void);

void __attribute__((visibility ("internal"))) ym_type_done (void);

extern "C"
{
	extern const struct cpifaceplayerstruct __attribute__((visibility ("internal"))) ymPlayer;
}

#endif
