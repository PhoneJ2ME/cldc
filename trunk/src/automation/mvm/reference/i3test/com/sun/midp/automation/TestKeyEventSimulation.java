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

package com.sun.midp.automation;

import com.sun.midp.i3test.*;
import java.util.*;

/**
 * i3test for keyboard events simulation
 */
public class TestKeyEventSimulation extends TestCase {
    /** URL of suite to install */
    private static final String SUITE_URL = 
        "http://localhost/~leonid/TextInput.jad";

    /** Midlet suite storage */
    private AutoSuiteStorage storage = null;

    /** Descriptor of the midlet suite */
    private AutoSuiteDescriptor suite = null;

    /** Descriptor of the midlet */
    private AutoMIDletDescriptor midletDescr = null;

    /**
     * Installs the test suite.
     */
    void installTestSuites() {
        Automation a = Automation.getInstance();

        declare("Install suite");
        storage = a.getStorage();

        try {
            suite = storage.installSuite(SUITE_URL);
        } catch (Exception e) {
        }

        assertNotNull("Failed to install suite", suite);

        Vector midlets = suite.getSuiteMIDlets();
        midletDescr = (AutoMIDletDescriptor)midlets.elementAt(0);
    }

    /**
     * Tests midlets switching
     */
    void testKeyEventSimulation() {
        installTestSuites();

        Automation a = Automation.getInstance();
        declare("Get AutoEventFactory instance");
        AutoEventFactory eventFactory = a.getEventFactory();
        assertNotNull("Failed to get AutoEventFactory instance", eventFactory);

        declare("Run suite");
        AutoMIDlet midlet = midletDescr.start(null);

        // Initial state is 'PAUSED'.
        midlet.switchTo(AutoMIDletLifeCycleState.ACTIVE, true);

        assertTrue("Invalid state of the midlet", midlet.
            getLifeCycleState().equals(AutoMIDletLifeCycleState.ACTIVE));

        midlet.switchTo(AutoMIDletForegroundState.FOREGROUND, true);


        // simulate 'A'
        AutoKeyEvent eventP, eventR;
        eventP = eventFactory.createKeyEvent('A', AutoKeyState.PRESSED);
        eventR = eventFactory.createKeyEvent('A', AutoKeyState.RELEASED);

        a.simulateEvents(eventP);
        a.simulateEvents(eventR);

        try {
            Thread.sleep(500);
        } catch (InterruptedException ie) {
        }


        // simulate 'B'
        a.simulateKeyEvent('b', AutoKeyState.PRESSED, 0);
        a.simulateKeyEvent('b', AutoKeyState.RELEASED, 0);
        
        try {
            Thread.sleep(500);
        } catch (InterruptedException ie) {
        }
        

        // simulate 'C'
        a.simulateKeyClick('c', 500);

        // simulate right soft key click
        a.simulateKeyClick(AutoKeyCode.SOFT2, 500);

        // simulate down key click
        a.simulateKeyClick(AutoKeyCode.DOWN, 500);

        // simulate select key click
        a.simulateKeyClick(AutoKeyCode.SELECT, 2500); 

        midlet.switchTo(AutoMIDletLifeCycleState.DESTROYED, true);

        uninstallTestSuites();
    }
    
    /**
     * Uninstalls the test suite.
     */
    void uninstallTestSuites() {
        declare("Uninstall suites");
        boolean exceptionThrown = false;
        try {
            storage.uninstallSuite(suite);
        } catch (Exception e) {
            exceptionThrown = true;
        }
        assertFalse("Failed to uninstall suites", exceptionThrown);
    }
    
    /**
     * Run tests
     */
    public void runTests() {
        testKeyEventSimulation();
    }
}
