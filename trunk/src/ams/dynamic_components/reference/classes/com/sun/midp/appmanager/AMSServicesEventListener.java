/*
 *
 *
 * Copyright  1990-2007 Sun Microsystems, Inc. All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 only, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details (a copy is
 * included at /legal/license.txt).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this work; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 or visit www.sun.com if you need additional
 * information or have any questions.
 */

package com.sun.midp.appmanager;

import com.sun.midp.events.Event;
import com.sun.midp.events.EventQueue;
import com.sun.midp.events.EventListener;
import com.sun.midp.events.NativeEvent;
import com.sun.midp.events.EventTypes;

/**
 * Listener for AMS Services events.
 */
class AMSServicesEventListener implements EventListener {
    private AMSServicesEventConsumer eventConsumer;

    /**
     * Package private constructor.
     *
     * @param  eventQueue reference to the event queue
     * @param  amsServicesEventConsumer comsumer that will process events
     *                                  received by this listener
     */
    AMSServicesEventListener(
            EventQueue eventQueue,
            AMSServicesEventConsumer amsServicesEventConsumer) {
        eventQueue.registerEventListener(
            EventTypes.RESTART_MIDLET_EVENT, this);
        eventConsumer = amsServicesEventConsumer;
    }

    /**
     * Preprocess an event that is being posted to the event queue.
     *
     * @param event event being posted
     *
     * @param waitingEvent previous event of this type waiting in the
     *     queue to be processed
     *
     * @return true to allow the post to continue, false to not post the
     *     event to the queue
     */
    public boolean preprocess(Event event, Event waitingEvent) {
        return true;
    }

    /**
     * Processes events.
     *
     * @param event event to process
     */
    public void process(Event event) {
        if (event.getType() == EventTypes.RESTART_MIDLET_EVENT) {
            NativeEvent ne = (NativeEvent)event;
            eventConsumer.handleRestartMIDletEvent(ne.intParam1, ne.intParam2,
                    ne.stringParam1, ne.stringParam2);
        }
    }
}
