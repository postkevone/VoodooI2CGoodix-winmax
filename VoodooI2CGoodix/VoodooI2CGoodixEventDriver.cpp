//
//  VoodooI2CGoodixEventDriver.cpp
//  VoodooI2CGoodix
//
//  Created by Larry Davis on 1/5/20.
//  Copyright © 2020 lazd. All rights reserved.
//

#include "VoodooI2CGoodixEventDriver.hpp"

#include "VoodooI2CGoodixEventDriver.hpp"
#include <IOKit/hid/IOHIDInterface.h>
#include <IOKit/IOLib.h>

#define super IOHIDEventService
OSDefineMetaClassAndStructors(VoodooI2CGoodixEventDriver, IOHIDEventService);

bool VoodooI2CGoodixEventDriver::didTerminate(IOService* provider, IOOptionBits options, bool* defer) {
//    if (hid_interface)
//        hid_interface->close(this);
//    hid_interface = NULL;

    return super::didTerminate(provider, options, defer);
}

void VoodooI2CGoodixEventDriver::reportTouches(struct Touch touches[], int numTouches) {
    AbsoluteTime timestamp;
    clock_get_uptime(&timestamp);

    IOLog("Need to report %d touches", numTouches);

    if (numTouches == 1) {
        Touch touch = touches[0];
//        VoodooI2CDigitiserTransducer* transducer = OSDynamicCast(VoodooI2CDigitiserTransducer, transducers->getObject(0));
//        UInt32 buttons = transducer->tip_switch.value();

        IOFixed x = ((touch.x * 1.0f) / multitouch_interface->logical_max_x) * 65535;
        IOFixed y = ((touch.y * 1.0f) / multitouch_interface->logical_max_y) * 65535;

        dispatchDigitizerEventWithTiltOrientation(timestamp, 0, kDigitiserTransducerFinger, 0x1, 0x1, x, y);
    }
    else {
        // Todo: move the cursor directly to the location between the fingers?

        // Send a multitouch event for scrolls, scales, etc
        for (int i = 0; i < numTouches; i++) {
            Touch touch = touches[i];

            IOLog("Touch %d at %d,%d with max %d,%d", i, touch.x, touch.y, multitouch_interface->logical_max_x, multitouch_interface->logical_max_y);
            VoodooI2CDigitiserTransducer* transducer = OSDynamicCast(VoodooI2CDigitiserTransducer, transducers->getObject(i));
            transducer->type = kDigitiserTransducerFinger;

            transducer->is_valid = true;

            if (multitouch_interface) {
                transducer->logical_max_x = multitouch_interface->logical_max_x;
                transducer->logical_max_y = multitouch_interface->logical_max_y;
            }

            transducer->coordinates.x.update(touch.x, timestamp);
            transducer->coordinates.y.update(touch.y, timestamp);

            // Todo: do something with touch->width to determine if it's a contact?
            transducer->tip_switch.update(1, timestamp);

            transducer->id = i;
            transducer->secondary_id = i;
        }

        VoodooI2CMultitouchEvent event;
        event.contact_count = numTouches;
        event.transducers = transducers;
        if (multitouch_interface) {
            multitouch_interface->handleInterruptReport(event, timestamp);
        }
    }
}

const char* VoodooI2CGoodixEventDriver::getProductName() {
    return "Goodix HID Device";
}

bool VoodooI2CGoodixEventDriver::handleStart(IOService* provider) {
    if(!super::handleStart(provider)) {
        return false;
    }

//    hid_interface = OSDynamicCast(IOHIDInterface, provider);
//
//    if (!hid_interface)
//        return false;

    name = getProductName();

    publishMultitouchInterface();

    transducers = OSArray::withCapacity(GOODIX_MAX_CONTACTS);
    if (!transducers) {
        return false;
    }
    DigitiserTransducerType type = kDigitiserTransducerFinger;
    for (int i = 0; i < GOODIX_MAX_CONTACTS; i++) {
        VoodooI2CDigitiserTransducer* transducer = VoodooI2CDigitiserTransducer::transducer(type, NULL);
        transducers->setObject(transducer);
    }

    setDigitizerProperties();

//    PMinit();
//    hid_interface->joinPMtree(this);
//    registerPowerDriver(this, VoodooI2CIOPMPowerStates, kVoodooI2CIOPMNumberPowerStates);

    multitouch_interface->registerService();

    return true;
}

void VoodooI2CGoodixEventDriver::handleStop(IOService* provider) {
    unpublishMultitouchInterface();

    if (transducers) {
        for (int i = 0; i < transducers->getCount(); i++) {
            OSObject* object = transducers->getObject(i);
            if (object) {
                object->release();
            }
        }
        OSSafeReleaseNULL(transducers);
    }

//    PMstop();
    super::handleStop(provider);
}

IOReturn VoodooI2CGoodixEventDriver::publishMultitouchInterface() {
    multitouch_interface = new VoodooI2CMultitouchInterface();
    if (!multitouch_interface) {
        IOLog("%s::No memory to allocate VoodooI2CMultitouchInterface instance\n", getName());
        goto multitouch_exit;
    }
    if (!multitouch_interface->init(NULL)) {
        IOLog("%s::Failed to init multitouch interface\n", getName());
        goto multitouch_exit;
    }
    if (!multitouch_interface->attach(this)) {
        IOLog("%s::Failed to attach multitouch interface\n", getName());
        goto multitouch_exit;
    }
    if (!multitouch_interface->start(this)) {
        IOLog("%s::Failed to start multitouch interface\n", getName());
        goto multitouch_exit;
    }
    // Assume we are a touchscreen for now
    multitouch_interface->setProperty(kIOHIDDisplayIntegratedKey, true);
    // Goodix's Vendor Id
    multitouch_interface->setProperty(kIOHIDVendorIDKey, 0x0416, 32);
    multitouch_interface->setProperty(kIOHIDProductIDKey, 0x0416, 32);
    IOLog("%s::Published multitouch interface\n", getName());
    return kIOReturnSuccess;
multitouch_exit:
    unpublishMultitouchInterface();
    return kIOReturnError;
}

void VoodooI2CGoodixEventDriver::initializeMultitouchInterface(int x, int y) {
    if (multitouch_interface) {
        IOLog("%s::Initializing multitouch interface with dimensions %d,%d\n", getName(), x, y);
        multitouch_interface->physical_max_x = x;
        multitouch_interface->physical_max_y = y;
        multitouch_interface->logical_max_x = x;
        multitouch_interface->logical_max_y = y;
    }
}

void VoodooI2CGoodixEventDriver::unpublishMultitouchInterface() {
    if (multitouch_interface) {
        IOLog("%s::Unpublishing multitouch interface\n", getName());
        multitouch_interface->stop(this);
        multitouch_interface->release();
        multitouch_interface = NULL;
    }
}

void VoodooI2CGoodixEventDriver::setDigitizerProperties() {
    OSDictionary* properties = OSDictionary::withCapacity(2);

    if (!properties)
        return;

    properties->setObject("Transducer Count", OSNumber::withNumber(GOODIX_MAX_CONTACTS, 32));

    setProperty("Digitizer", properties);
}

IOReturn VoodooI2CGoodixEventDriver::setPowerState(unsigned long whichState, IOService* whatDevice) {
    return kIOPMAckImplied;
}

bool VoodooI2CGoodixEventDriver::start(IOService* provider) {
    if (!super::start(provider))
        return false;

    setProperty("VoodooI2CServices Supported", kOSBooleanTrue);

    return true;
}
