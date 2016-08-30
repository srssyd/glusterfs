//
// Created by syd on 16-8-27.
//

#ifndef _IO_BATCH_MESSAGES_H
#define _IO_BATCH_MESSAGES_H

/*Copyright (c) 2015 Red Hat, Inc. <http://www.redhat.com>
  This file is part of GlusterFS.

  This file is licensed to you under your choice of the GNU Lesser
  General Public License, version 3 or any later version (LGPLv3 or
  later), or the GNU General Public License, version 2 (GPLv2), in all
  cases as published by the Free Software Foundation.
*/

#include "glfs-message-id.h"

/*! \file io-batch-messages.h
 *  \brief IO_CACHE log-message IDs and their descriptions
 *
 */

/* NOTE: Rules for message additions
 * 1) Each instance of a message is _better_ left with a unique message ID, even
 *    if the message format is the same. Reasoning is that, if the message
 *    format needs to change in one instance, the other instances are not
 *    impacted or the new change does not change the ID of the instance being
 *    modified.
 * 2) Addition of a message,
 *       - Should increment the GLFS_NUM_MESSAGES
 *       - Append to the list of messages defined, towards the end
 *       - Retain macro naming as glfs_msg_X (for redability across developers)
 * NOTE: Rules for message format modifications
 * 3) Check acorss the code if the message ID macro in question is reused
 *    anywhere. If reused then then the modifications should ensure correctness
 *    everywhere, or needs a new message ID as (1) above was not adhered to. If
 *    not used anywhere, proceed with the required modification.
 * NOTE: Rules for message deletion
 * 4) Check (3) and if used anywhere else, then cannot be deleted. If not used
 *    anywhere, then can be deleted, but will leave a hole by design, as
 *    addition rules specify modification to the end of the list and not filling
 *    holes.
 */

#define GLFS_IO_BATCH_BASE                   GLFS_MSGID_COMP_IO_CACHE
#define GLFS_IO_BATCH_NUM_MESSAGES           1
#define GLFS_MSGID_END  (GLFS_IO_BATCH_BASE + GLFS_IO_BATCH_NUM_MESSAGES + 1)

/* Messages with message IDs */
#define glfs_msg_start_x GLFS_IO_BATCH_BASE, "Invalid: Start of messages"




/*!
 * @messageid
 * @diagnosis
 * @recommendedaction  None
 *
 */

#define IO_BATCH_MEMORY_INIT_FAILED        (GLFS_IO_BATCH_BASE + 1)


/*------------*/
#define glfs_msg_end_x GLFS_MSGID_END, "Invalid: End of messages"


#endif

