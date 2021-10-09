/*
 * =============================================================================
 * Copyright (c) 2016  Texas Instruments Inc.
 * Copyright (C) 2021 XiaoMi, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.See the GNU General Public License for more details.
 *
 * File:
 *     tas256x-codec.h
 *
 * Description:
 *     header file for tas256x-codec.c
 *
 * =============================================================================
 */

#ifndef _TAS256X_CODEC_H
#define _TAS256X_CODEC_H

#include "tas256x.h"

int tas256x_register_codec(struct tas256x_priv *p_tas256x);
int tas256x_deregister_codec(struct tas256x_priv *p_tas256x);
int tas256x_load_config(struct tas256x_priv *p_tas256x);
void tas_reload(struct tas256x_priv *p_tas256x, int chn);

#endif /* _TAS256X_CODEC_H */
