/*
 * Copyright (c) 2017 Roc Streaming authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/**
 * \file roc/sender.h
 * \brief Roc sender.
 */

#ifndef ROC_SENDER_H_
#define ROC_SENDER_H_

#include "roc/config.h"
#include "roc/context.h"
#include "roc/endpoint.h"
#include "roc/frame.h"
#include "roc/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Sender peer.
 *
 * Sender gets an audio stream from the user, encodes it into network packets, and
 * transmits them to a remote receiver.
 *
 * **Context**
 *
 * Sender is automatically attached to a context when opened and detached from it when
 * closed. The user should not close the context until the sender is closed.
 *
 * Sender work consists of two parts: stream encoding and packet transmission. The
 * encoding part is performed in the sender itself, and the transmission part is
 * performed in the context network worker threads.
 *
 * **Life cycle**
 *
 * - A sender is created using roc_sender_open().
 *
 * - Optionally, the sender parameters may be fine-tuned using `roc_sender_set_*()`
 *   functions.
 *
 * - The sender either binds local endpoints using roc_sender_bind(), allowing receivers
 *   connecting to them, or itself connects to remote receiver endpoints using
 *   roc_sender_connect(). What approach to use is up to the user.
 *
 * - The audio stream is iteratively written to the sender using roc_sender_write(). The
 *   sender encodes the stream into packets and send to connected receiver(s).
 *
 * - The sender is destroyed using roc_sender_close().
 *
 * **Slots, interfaces, and endpoints**
 *
 * Sender has one or multiple **slots**, which may be independently bound or connected.
 * Slots may be used to connect sender to multiple receivers. Slots are numbered from
 * zero and are created automatically. In simple cases just use \c ROC_SLOT_DEFAULT.
 *
 * Each slot has its own set of *interfaces*, one per each type defined in \ref
 * roc_interface. The interface defines the type of the communication with the remote peer
 * and the set of the protocols supported by it.
 *
 * Supported actions with the interface:
 *
 *  - Call roc_sender_bind() to bind the interface to a local \ref roc_endpoint. In this
 *    case the sender accepts connections from receivers and sends media stream to all
 *    connected receivers.
 *
 *  - Call roc_sender_connect() to connect the interface to a remote \ref roc_endpoint.
 *    In this case the sender initiates connection to the receiver and starts sending
 *    media stream to it.
 *
 * Supported interface configurations:
 *
 *   - Connect \c ROC_INTERFACE_CONSOLIDATED to a remote endpoint (e.g. be an RTSP
 *     client).
 *   - Bind \c ROC_INTERFACE_CONSOLIDATED to a local endpoint (e.g. be an RTSP server).
 *   - Connect \c ROC_INTERFACE_AUDIO_SOURCE, \c ROC_INTERFACE_AUDIO_REPAIR (optionally,
 *     for FEC), and \c ROC_INTERFACE_AUDIO_CONTROL (optionally, for control messages)
 *     to remote endpoints (e.g. be an RTP/FECFRAME/RTCP sender).
 *
 * **FEC scheme**
 *
 * If \c ROC_INTERFACE_CONSOLIDATED is used, it automatically creates all necessary
 * transport interfaces and the user should not bother about them.
 *
 * Otherwise, the user should manually configure \c ROC_INTERFACE_AUDIO_SOURCE and
 * \c ROC_INTERFACE_AUDIO_REPAIR interfaces:
 *
 *  - If FEC is disabled (\ref ROC_FEC_ENCODING_DISABLE), only
 *    \c ROC_INTERFACE_AUDIO_SOURCE should be configured. It will be used to transmit
 *    audio packets.
 *
 *  - If FEC is enabled, both \c ROC_INTERFACE_AUDIO_SOURCE and
 *    \c ROC_INTERFACE_AUDIO_REPAIR interfaces should be configured. The second interface
 *    will be used to transmit redundant repair data.
 *
 * The protocols for the two interfaces should correspond to each other and to the FEC
 * scheme. For example, if \c ROC_FEC_RS8M is used, the protocols should be
 * \c ROC_PROTO_RTP_RS8M_SOURCE and \c ROC_PROTO_RS8M_REPAIR.
 *
 * **Sample rate**
 *
 * If the sample rate of the user frames and the sample rate of the network packets are
 * different, the sender employs resampler to convert one rate to another.
 *
 * Resampling is a quite time-consuming operation. The user can choose between completely
 * disabling resampling (and so use the same rate for frames and packets) or several
 * resampler profiles providing different compromises between CPU consumption and quality.
 *
 * **Clock source**
 *
 * Sender should encode samples at a constant rate that is configured when the sender
 * is created. There are two ways to accomplish this:
 *
 *  - If the user enabled internal clock (\c ROC_CLOCK_INTERNAL), the sender employs a
 *    CPU timer to block writes until it's time to encode the next bunch of samples
 *    according to the configured sample rate.

 *    This mode is useful when the user gets samples from a non-realtime source, e.g.
 *    from an audio file.
 *
 *  - If the user enabled external clock (\c ROC_CLOCK_EXTERNAL), the samples written to
 *    the sender are encoded and sent immediately, and hence the user is responsible to
 *    call write operation according to the sample rate.
 *
 *    This mode is useful when the user gets samples from a realtime source with its own
 *    clock, e.g. from an audio device. Internal clock should not be used in this case
 *    because the audio device and the CPU might have slightly different clocks, and the
 *    difference will eventually lead to an underrun or an overrun.
 *
 * **Thread safety**
 *
 * Can be used concurrently.
 */
typedef struct roc_sender roc_sender;

/** Open a new sender.
 *
 * Allocates and initializes a new sender, and attaches it to the context.
 *
 * **Parameters**
 *  - \p context should point to an opened context
 *  - \p config should point to an initialized config
 *  - \p result should point to an unitialized roc_sender pointer
 *
 * **Returns**
 *  - returns zero if the sender was successfully created
 *  - returns a negative value if the arguments are invalid
 *  - returns a negative value on resource allocation failure
 *
 * **Ownership**
 *  - doesn't take or share the ownership of \p config; it may be safely deallocated
 *    after the function returns
 *  - passes the ownership of \p result to the user; the user is responsible to call
 *    roc_sender_close() to free it
 */
ROC_API int roc_sender_open(roc_context* context,
                            const roc_sender_config* config,
                            roc_sender** result);

/** Set sender interface outgoing address.
 *
 * Optional. Should be used only when connecting an interface to a remote endpoint.
 *
 * If set, explicitly defines the IP address of the OS network interface from which to
 * send the outgoing packets. If not set, the outgoing interface is selected automatically
 * by the OS, depending on the remote endpoint address.
 *
 * It is allowed to set outgoing address to `0.0.0.0` (for IPv4) or to `::` (for IPv6),
 * to achieve the same behavior as if it wasn't set, i.e. to let the OS to select the
 * outgoing interface automatically.
 *
 * By default, the outgoing address is not set.
 *
 * Each slot's interface can have only one outgoing address. The function should be called
 * before calling roc_sender_connect() for this slot and interface. It should not be
 * called when calling roc_sender_bind() for the interface.
 *
 * Automatically initializes slot with given index if it's used first time.
 *
 * **Parameters**
 *  - \p sender should point to an opened sender
 *  - \p slot specifies the sender slot
 *  - \p iface specifies the sender interface
 *  - \p ip should be IPv4 or IPv6 address
 *
 * **Returns**
 *  - returns zero if the outgoing interface was successfully set
 *  - returns a negative value if the arguments are invalid
 *  - returns a negative value if an error occurred
 *
 * **Ownership**
 *  - doesn't take or share the ownership of \p ip; it may be safely deallocated
 *    after the function returns
 */
ROC_API int roc_sender_set_outgoing_address(roc_sender* sender,
                                            roc_slot slot,
                                            roc_interface iface,
                                            const char* ip);

/** Set sender interface address reuse option.
 *
 * Optional.
 *
 * When set to true, SO_REUSEADDR is enabled for interface socket, regardless of socket
 * type, unless binding to ephemeral port (port explicitly set to zero).
 *
 * When set to false, SO_REUSEADDR is enabled only for multicast sockets, unless binding
 * to ephemeral port (port explicitly set to zero).
 *
 * By default set to false.
 *
 * For TCP-based protocols, SO_REUSEADDR allows immediate reuse of recently closed socket
 * in TIME_WAIT state, which may be useful you want to be able to restart server quickly.
 *
 * For UDP-based protocols, SO_REUSEADDR allows multiple processes to bind to the same
 * address, which may be useful if you're using socket activation mechanism.
 *
 * Automatically initializes slot with given index if it's used first time.
 *
 * **Parameters**
 *  - \p sender should point to an opened sender
 *  - \p slot specifies the sender slot
 *  - \p iface specifies the sender interface
 *  - \p enabled should be 0 or 1
 *
 * **Returns**
 *  - returns zero if the multicast group was successfully set
 *  - returns a negative value if the arguments are invalid
 *  - returns a negative value if an error occurred
 */
ROC_API int roc_sender_set_reuseaddr(roc_sender* sender,
                                     roc_slot slot,
                                     roc_interface iface,
                                     int enabled);

/** Connect the sender interface to a remote receiver endpoint.
 *
 * Checks that the endpoint is valid and supported by the interface, allocates
 * a new outgoing port, and connects it to the remote endpoint.
 *
 * Each slot's interface can be bound or connected only once.
 * May be called multiple times for different slots or interfaces.
 *
 * Automatically initializes slot with given index if it's used first time.
 *
 * **Parameters**
 *  - \p sender should point to an opened sender
 *  - \p slot specifies the sender slot
 *  - \p iface specifies the sender interface
 *  - \p endpoint specifies the receiver endpoint
 *
 * **Returns**
 *  - returns zero if the sender was successfully connected
 *  - returns a negative value if the arguments are invalid
 *  - returns a negative value on resource allocation failure
 *
 * **Ownership**
 *  - doesn't take or share the ownership of \p endpoint; it may be safely deallocated
 *    after the function returns
 */
ROC_API int roc_sender_connect(roc_sender* sender,
                               roc_slot slot,
                               roc_interface iface,
                               const roc_endpoint* endpoint);

/** Encode samples to packets and transmit them to the receiver.
 *
 * Encodes samples to packets and enqueues them for transmission by the network worker
 * thread of the context.
 *
 * If \c ROC_CLOCK_INTERNAL is used, the function blocks until it's time to transmit the
 * samples according to the configured sample rate. The function returns after encoding
 * and enqueuing the packets, without waiting when the packets are actually transmitted.
 *
 * Until the sender is connected to at least one receiver, the stream is just dropped.
 * If the sender is connected to multiple receivers, the stream is duplicated to
 * each of them.
 *
 * **Parameters**
 *  - \p sender should point to an opened, bound, and connected sender
 *  - \p frame should point to a valid frame with an array of samples to send
 *
 * **Returns**
 *  - returns zero if all samples were successfully encoded and enqueued
 *  - returns a negative value if the arguments are invalid
 *  - returns a negative value on resource allocation failure
 *
 * **Ownership**
 *  - doesn't take or share the ownership of \p frame; it may be safely deallocated
 *    after the function returns
 */
ROC_API int roc_sender_write(roc_sender* sender, const roc_frame* frame);

/** Close the sender.
 *
 * Deinitializes and deallocates the sender, and detaches it from the context. The user
 * should ensure that nobody uses the sender during and after this call. If this
 * function fails, the sender is kept opened and attached to the context.
 *
 * **Parameters**
 *  - \p sender should point to an opened sender
 *
 * **Returns**
 *  - returns zero if the sender was successfully closed
 *  - returns a negative value if the arguments are invalid
 *
 * **Ownership**
 *  - ends the user ownership of \p sender; it can't be used anymore after the
 *    function returns
 */
ROC_API int roc_sender_close(roc_sender* sender);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROC_SENDER_H_ */
