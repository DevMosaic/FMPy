/* This file is part of FMPy. See LICENSE.txt for license information. */

#if defined(_WIN32)
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#elif defined(__APPLE__)
#include <libgen.h>
#include <sys/syslimits.h>
#else
#define _GNU_SOURCE
#include <libgen.h>
#include <linux/limits.h>
#endif

#include <mpack.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include "fmi3Functions.h"

#include "FMI2.h"
#include "FMI3.h"

#include "FMUContainer.h"


#define CHECK_STATUS(S) status = S; if (status > FMIWarning) goto END


#ifdef _WIN32
static DWORD WINAPI instanceDoStep(LPVOID lpParam) {

    Component *c = (Component *)lpParam;

    while (true) {

        DWORD dwWaitResult = WaitForSingleObject(c->mutex, INFINITE);

        if (c->terminate) {
            return TRUE;
        }

        if (c->doStep) {
            switch (c->instance->fmiMajorVersion) {
            case FMIMajorVersion2:
                c->status = FMI2DoStep(c->instance, c->currentCommunicationPoint, c->communicationStepSize, c->noSetFMUStatePriorToCurrentPoint);
                break;
            case FMIMajorVersion3: ;
                fmi3Boolean eventHandlingNeeded;
                fmi3Boolean terminateSimulation;
                fmi3Boolean earlyReturn;
                fmi3Float64 lastSuccessfulTime;
    
                c->status = FMI3DoStep(c->instance, c->currentCommunicationPoint, c->communicationStepSize, c->noSetFMUStatePriorToCurrentPoint, &eventHandlingNeeded, &terminateSimulation, &earlyReturn, &lastSuccessfulTime);
                break;
            default:
                break;
            }
            c->doStep = false;
        }

        BOOL success = ReleaseMutex(c->mutex);
    }

    return TRUE;
}
#else
static void* instanceDoStep(void *arg) {

    Component *c = (Component *)arg;

    while (true) {

        pthread_mutex_lock(&c->mutex);

        if (c->terminate) {
            return NULL;
        }

        if (c->doStep) {
            switch (c->instance->fmiMajorVersion) {
            case FMIMajorVersion2:
                c->status = FMI2DoStep(c->instance, c->currentCommunicationPoint, c->communicationStepSize, c->noSetFMUStatePriorToCurrentPoint);
                break;
            case FMIMajorVersion3: ;
                fmi3Boolean eventHandlingNeeded;
                fmi3Boolean terminateSimulation;
                fmi3Boolean earlyReturn;
                fmi3Float64 lastSuccessfulTime;
    
                c->status = FMI3DoStep(c->instance, c->currentCommunicationPoint, c->communicationStepSize, c->noSetFMUStatePriorToCurrentPoint, &eventHandlingNeeded, &terminateSimulation, &earlyReturn, &lastSuccessfulTime);
                break;
            default:
                break;
            }
            c->doStep = false;
        }

        pthread_mutex_unlock(&c->mutex);
    }

    return NULL;
}
#endif


static void logFMIMessage(FMIInstance *instance, FMIStatus status, const char *category, const char *message) {

    if (!instance) {
        return;
    }

    System* s = instance->userData;

    if (!s || !s->logMessage) {
        return;
    }

    size_t message_len = strlen(message);
    size_t instanceName_len = strlen(instance->name);
    size_t total_len = message_len + instanceName_len + 5;

    char *buf = malloc(total_len);

    snprintf(buf, total_len, "[%s]: %s", instance->name, message);

    switch (s->fmiMajorVersion) {
    case FMIMajorVersion2:
        ((fmi2CallbackLogger)s->logMessage)(s->instanceEnvironment, s->instanceName, status, category, buf);
        break;
    case FMIMajorVersion3:
        ((fmi3LogMessageCallback)s->logMessage)(s->instanceEnvironment, status, category, buf);
        break;
    default:
        break;
    }

    free(buf);
}

static void logFunctionCall(FMIInstance *instance, FMIStatus status, const char *message, ...) {

    if (!instance) {
        return;
    }

    System *s = instance->userData;

    if (!s || !s->logMessage) {
        return;
    }

    char buf[FMI_MAX_MESSAGE_LENGTH] = "";

    const size_t len = snprintf(buf, FMI_MAX_MESSAGE_LENGTH, "[%s] ", instance->name);

    va_list args;

    va_start(args, message);
    vsnprintf(&buf[len], FMI_MAX_MESSAGE_LENGTH - len, message, args);
    va_end(args);

    switch (s->fmiMajorVersion) {
    case FMIMajorVersion2:
        ((fmi2CallbackLogger)s->logMessage)(s->instanceEnvironment, s->instanceName, status, "logDebug", buf);
        break;
    case FMIMajorVersion3:
        ((fmi3LogMessageCallback)s->logMessage)(s->instanceEnvironment, status, "logDebug", buf);
        break;
    default:
        break;
    }
}

#define CHECK_STATUS(S) status = S; if (status > FMIWarning) goto END

FMIStatus getVariable(
    FMIInstance *instance,
    FMIVariableType variableType,
    const FMIValueReference* valueReference,
    void* value) {

    FMIStatus status = FMIOK;

    switch (variableType) {
    case FMIFloat32Type:
        CHECK_STATUS(FMI3GetFloat32(instance, valueReference, 1, value, 1));
        break;
    case FMIFloat64Type:
        switch (instance->fmiMajorVersion) {
        case FMIMajorVersion2: ;
            fmi2Real v;
            CHECK_STATUS(FMI2GetReal(instance, valueReference, 1, &v));
            *((fmi3Float64 *) value) = (fmi3Float64) v;
            break;
        case FMIMajorVersion3:
            CHECK_STATUS(FMI3GetFloat64(instance, valueReference, 1, value, 1));
            break;
        default:
            status = FMIError;
            break;
        }
        break;
    case FMIInt8Type:
        CHECK_STATUS(FMI3GetInt8(instance, valueReference, 1, value, 1));
        break;
    case FMIUInt8Type:
        CHECK_STATUS(FMI3GetUInt8(instance, valueReference, 1, value, 1));
        break;
    case FMIInt16Type:
        CHECK_STATUS(FMI3GetInt16(instance, valueReference, 1, value, 1));
        break;
    case FMIUInt16Type:
        CHECK_STATUS(FMI3GetUInt16(instance, valueReference, 1, value, 1));
        break;
    case FMIInt32Type:
        switch (instance->fmiMajorVersion) {
        case FMIMajorVersion2: ;
            {
            fmi2Integer v;
            CHECK_STATUS(FMI2GetInteger(instance, valueReference, 1, &v));
            *((fmi3Int32 *) value) = (fmi3Int32) v;
            break;
            }
        case FMIMajorVersion3:
            CHECK_STATUS(FMI3GetInt32(instance, valueReference, 1, value, 1));
            break;
        default:
            status = FMIError;
            break;
        }
        break;
    case FMIUInt32Type:
        CHECK_STATUS(FMI3GetUInt32(instance, valueReference, 1, value, 1));
        break;
    case FMIInt64Type:
        switch (instance->fmiMajorVersion) {
        case FMIMajorVersion2: ;
            fmi2Integer v;
            CHECK_STATUS(FMI2GetInteger(instance, valueReference, 1, &v));
            *((fmi3Int64 *) value) = (fmi3Int64) v;
            break;
        case FMIMajorVersion3:
            CHECK_STATUS(FMI3GetInt64(instance, valueReference, 1, value, 1));
            break;
        default:
            status = FMIError;
            break;
        }
        break;
    case FMIUInt64Type:
        CHECK_STATUS(FMI3GetUInt64(instance, valueReference, 1, value, 1));
        break;
    case FMIBooleanType:
        switch (instance->fmiMajorVersion) {
        case FMIMajorVersion2: ;
            fmi2Boolean v;
            CHECK_STATUS(FMI2GetBoolean(instance, valueReference, 1, &v));
            *((fmi3Boolean *) value) = (fmi3Boolean) v;
            break;
        case FMIMajorVersion3:
            CHECK_STATUS(FMI3GetBoolean(instance, valueReference, 1, value, 1));
            break;
        default:
            status = FMIError;
            break;
        }
        break;
    case FMIStringType:
        switch (instance->fmiMajorVersion) {
        case FMIMajorVersion2:
            CHECK_STATUS(FMI2GetString(instance, valueReference, 1, value));
            break;
        case FMIMajorVersion3:
            CHECK_STATUS(FMI3GetString(instance, valueReference, 1, value, 1));
            break;
        default:
            status = FMIError;
            break;
        }
        break;
    // TODO: implement
    case FMIBinaryType:
        status = FMIError;
        break;
    case FMIClockType:
        status = FMIError;
        break;
    default:
        status = FMIError;
    }

END:
    return status;
}

/*
 * `value` should be Float64, Int64, UInt64, Boolean, or String.
 */
FMIStatus setVariable(
    FMIInstance *instance,
    FMIVariableType variableType,
    const FMIValueReference* valueReference,
    void* value) {

    FMIStatus status = FMIOK;

    switch (variableType) {
    case FMIFloat32Type:
        CHECK_STATUS(FMI3SetFloat32(instance, valueReference, 1, value, 1));
        break;
    case FMIFloat64Type:
        switch (instance->fmiMajorVersion) {
        case FMIMajorVersion2: ;
            fmi2Real v = *((fmi3Float64 *) value);
            CHECK_STATUS(FMI2SetReal(instance, valueReference, 1, &v));
            break;
        case FMIMajorVersion3:
            CHECK_STATUS(FMI3SetFloat64(instance, valueReference, 1, value, 1));
            break;
        default:
            status = FMIError;
            break;
        }
        break;
    case FMIInt8Type:
        CHECK_STATUS(FMI3SetInt8(instance, valueReference, 1, value, 1));
        break;
    case FMIUInt8Type:
        CHECK_STATUS(FMI3SetUInt8(instance, valueReference, 1, value, 1));
        break;
    case FMIInt16Type:
        CHECK_STATUS(FMI3SetInt16(instance, valueReference, 1, value, 1));
        break;
    case FMIUInt16Type:
        CHECK_STATUS(FMI3SetUInt16(instance, valueReference, 1, value, 1));
        break;
    case FMIInt32Type:
        switch (instance->fmiMajorVersion) {
        case FMIMajorVersion2: ;
            {
            fmi2Integer v = (fmi2Integer) *((fmi3Int32 *) value);
            CHECK_STATUS(FMI2SetInteger(instance, valueReference, 1, &v));
            break;
            }
        case FMIMajorVersion3:
            {
            CHECK_STATUS(FMI3SetInt32(instance, valueReference, 1, value, 1));
            break;
            }
        default:
            status = FMIError;
            break;
        }
        break;
    case FMIUInt32Type:
        CHECK_STATUS(FMI3SetUInt32(instance, valueReference, 1, value, 1));
        break;
    case FMIInt64Type:
        switch (instance->fmiMajorVersion) {
        case FMIMajorVersion2: ;
            fmi2Integer v = (fmi2Integer) *((fmi3Int64 *) value);
            CHECK_STATUS(FMI2SetInteger(instance, valueReference, 1, &v));
            break;
        case FMIMajorVersion3:
            CHECK_STATUS(FMI3SetInt64(instance, valueReference, 1, value, 1));
            break;
        default:
            status = FMIError;
            break;
        }
        break;
    case FMIUInt64Type:
        CHECK_STATUS(FMI3SetUInt64(instance, valueReference, 1, value, 1));
        break;
    case FMIBooleanType:
        switch (instance->fmiMajorVersion) {
        case FMIMajorVersion2: ;
            fmi2Boolean v = (fmi2Boolean) *((fmi3Boolean *) value);
            CHECK_STATUS(FMI2SetBoolean(instance, valueReference, 1, &v));
            break;
        case FMIMajorVersion3:
            CHECK_STATUS(FMI3SetBoolean(instance, valueReference, 1, value, 1));
            break;
        default:
            status = FMIError;
            break;
        }
        break;
    case FMIStringType:
        switch (instance->fmiMajorVersion) {
        case FMIMajorVersion2:
            CHECK_STATUS(FMI2SetString(instance, valueReference, 1, value));
            break;
        case FMIMajorVersion3:
            CHECK_STATUS(FMI3SetString(instance, valueReference, 1, value, 1));
            break;
        default:
            status = FMIError;
            break;
        }
        break;
    // TODO: implement
    case FMIBinaryType:
        status = FMIError;
        break;
    case FMIClockType:
        status = FMIError;
        break;
    default:
        status = FMIError;
    }

END:
    return status;
}

System* instantiateSystem(
    FMIMajorVersion fmiMajorVersion,
    const char* resourcesDir,
    const char* instanceName,
    void* logMessage,
    void* instanceEnvironment,
    bool loggingOn,
    bool visible) {

    char configFilename[4096] = "";
    strcpy(configFilename, resourcesDir);
    strcat(configFilename, "config.mp");

    // parse a file into a node tree
    mpack_tree_t tree;
    mpack_tree_init_filename(&tree, configFilename, 0);
    mpack_tree_parse(&tree);
    mpack_node_t root = mpack_tree_root(&tree);

#ifdef _DEBUG
    mpack_node_print_to_stdout(root);
#endif

    mpack_node_t parallelDoStep = mpack_node_map_cstr(root, "parallelDoStep");

    System* s = calloc(1, sizeof(System));

    s->fmiMajorVersion = fmiMajorVersion;
    s->instanceName = strdup(instanceName);
    s->instanceEnvironment = instanceEnvironment;
    s->logMessage = logMessage;
    s->parallelDoStep = mpack_node_bool(parallelDoStep);
    s->time = 0;

    mpack_node_t components = mpack_node_map_cstr(root, "components");

    s->nComponents = mpack_node_array_length(components);

    s->components = calloc(s->nComponents, sizeof(FMIInstance*));

    for (size_t i = 0; i < s->nComponents; i++) {
        Component* c = calloc(1, sizeof(Component));

        mpack_node_t component = mpack_node_array_at(components, i);

        mpack_node_t name = mpack_node_map_cstr(component, "name");
        char* _name = mpack_node_cstr_alloc(name, 1024);

        mpack_node_t componentFmiVersion = mpack_node_map_cstr(component, "fmiVersion");
        char* _componentFmiVersion = mpack_node_cstr_alloc(componentFmiVersion, 1024);

        FMIMajorVersion componentFmiMajorVersion;
        if (*_componentFmiVersion == '2') {
            componentFmiMajorVersion = FMIMajorVersion2;
        } else if (*_componentFmiVersion == '3') {
            componentFmiMajorVersion = FMIMajorVersion3;
        } else {
            return NULL;
        }

        mpack_node_t guid = mpack_node_map_cstr(component, "guid");
        char* _guid = mpack_node_cstr_alloc(guid, 1024);

        mpack_node_t modelIdentifier = mpack_node_map_cstr(component, "modelIdentifier");
        char* _modelIdentifier = mpack_node_cstr_alloc(modelIdentifier, 1024);

        char unzipdir[4069] = "";
        char componentResourcesDir[4069] = "";

#ifdef _WIN32
        PathCombine(unzipdir, resourcesDir, _modelIdentifier);
        PathCombine(componentResourcesDir, unzipdir, "resources");
#else
        sprintf(unzipdir, "%s/%s", resourcesDir, _modelIdentifier);
        sprintf(componentResourcesDir, "%s/%s", unzipdir, "resources");
#endif
        char componentResourcesUri[4069] = "";

        FMIPathToURI(componentResourcesDir, componentResourcesUri, 4096);

        char libraryPath[4069] = "";

        switch (componentFmiMajorVersion) {
        case FMIMajorVersion2:
            FMIPlatformBinaryPath(unzipdir, _modelIdentifier, FMIMajorVersion2, libraryPath, 4096);
            break;
        case FMIMajorVersion3:
            FMIPlatformBinaryPath(unzipdir, _modelIdentifier, FMIMajorVersion3, libraryPath, 4096);
            break;
        default:
            break;
        }

        FMIInstance* m = FMICreateInstance(_name, logFMIMessage, loggingOn ? logFunctionCall : NULL);
        FMILoadPlatformBinary(m, libraryPath);

        if (!m) {
            return NULL;
        }

        m->userData = s;

        switch (componentFmiMajorVersion) {
        case FMIMajorVersion2:
            if (FMI2Instantiate(m, componentResourcesDir, fmi2CoSimulation, _guid, visible, loggingOn) > FMIWarning) {
                return NULL;
            }
            break;
        case FMIMajorVersion3:
            // TODO: Add support for some FMI3 options if necessary
            //           (hasEventMode=false, mightReturnEarlyFromDoStep=true, etc.)
            if (FMI3InstantiateCoSimulation(m, _guid, componentResourcesDir, visible, loggingOn, true, false, NULL, 0, NULL ) > FMIWarning) {
                return NULL;
            }
            break;
        default:
            break;
        }

        c->instance = m;

        if (s->parallelDoStep) {
            c->doStep = false;
            c->terminate = false;
#ifdef _WIN32
            // TODO: check for invalid handles
            c->mutex = CreateMutexA(NULL, FALSE, NULL);
            c->thread = CreateThread(NULL, 0, instanceDoStep, c, 0, NULL);
#else
            // TODO: check return codes
            pthread_mutex_init(&c->mutex, NULL);
            pthread_create(&c->thread, NULL, &instanceDoStep, c);
#endif
        }

        s->components[i] = c;
    }

    mpack_node_t connections = mpack_node_map_cstr(root, "connections");

    s->nConnections = mpack_node_array_length(connections);

    s->connections = calloc(s->nConnections, sizeof(Connection));

    for (size_t i = 0; i < s->nConnections; i++) {
        mpack_node_t connection = mpack_node_array_at(connections, i);

        mpack_node_t type = mpack_node_map_cstr(connection, "type");
        s->connections[i].type = mpack_node_int(type);

        mpack_node_t startComponent = mpack_node_map_cstr(connection, "startComponent");
        s->connections[i].startComponent = mpack_node_u64(startComponent);

        mpack_node_t endComponent = mpack_node_map_cstr(connection, "endComponent");
        s->connections[i].endComponent = mpack_node_u64(endComponent);

        mpack_node_t startValueReference = mpack_node_map_cstr(connection, "startValueReference");
        s->connections[i].startValueReference = mpack_node_u32(startValueReference);

        mpack_node_t endValueReference = mpack_node_map_cstr(connection, "endValueReference");
        s->connections[i].endValueReference = mpack_node_u32(endValueReference);
    }

    mpack_node_t variables = mpack_node_map_cstr(root, "variables");

    s->nVariables = mpack_node_array_length(variables);

    s->variables = calloc(s->nVariables, sizeof(VariableMapping));

    for (size_t i = 0; i < s->nVariables; i++) {

        mpack_node_t variable = mpack_node_array_at(variables, i);

        mpack_node_t components = mpack_node_map_cstr(variable, "components");
        mpack_node_t valueReferences = mpack_node_map_cstr(variable, "valueReferences");
        mpack_node_t variableTypeNode = mpack_node_map_cstr(variable, "type");
        FMIVariableType variableType = mpack_node_int(variableTypeNode);

        const bool hasStartValue = mpack_node_map_contains_cstr(variable, "start");

        mpack_node_t start;

        if (hasStartValue) {
            start = mpack_node_map_cstr(variable, "start");
        }

        s->variables[i].size = mpack_node_array_length(components);
        s->variables[i].ci = calloc(s->variables[i].size, sizeof(size_t));
        s->variables[i].vr = calloc(s->variables[i].size, sizeof(FMIValueReference));

        for (size_t j = 0; j < s->variables[i].size; j++) {

            mpack_node_t component = mpack_node_array_at(components, j);
            mpack_node_t valueReference = mpack_node_array_at(valueReferences, j);

            const size_t ci = mpack_node_u64(component);
            const FMIValueReference vr = mpack_node_u32(valueReference);

            if (hasStartValue) {

                FMIStatus status = FMIOK;
                FMIInstance* m = s->components[ci]->instance;

                // TODO: Unpack start values for other FMI3 types
                switch (variableType) {
                case FMIRealType: {
                    const fmi3Float64 value = mpack_node_double(start);
                    status = setVariable(m, variableType, &vr, &value);
                    break;
                }
                case FMIIntegerType: {
                    const fmi3Int32 value = mpack_node_int(start);
                    status = setVariable(m, variableType, &vr, &value);
                    break;
                }
                case FMIInt64Type: {
                    const fmi3Int64 value = mpack_node_int(start);
                    status = setVariable(m, variableType, &vr, &value);
                    break;
                }
                case FMIBooleanType: {
                    const fmi3Boolean value = mpack_node_bool(start);
                    status = setVariable(m, variableType, &vr, &value);
                    break;
                }
                case FMIStringType: {
                    const char* value = mpack_node_cstr_alloc(start, 2048);
                    status = setVariable(m, variableType, &vr, &value);
                    MPACK_FREE((void *) value);
                    break;
                }
                default:
                    // TODO: log this
                    // logMessage(NULL, instanceName, fmi2Fatal, "logError", "Unknown type ID for variable index %d: %d.", j, variableType);
                    return NULL;
                    break;
                }

                if (status > FMIWarning) {
                    return NULL;
                }
            }

            s->variables[i].ci[j] = ci;
            s->variables[i].vr[j] = vr;
        }

    }

    // clean up and check for errors
    if (mpack_tree_destroy(&tree) != mpack_ok) {
        // TODO: log this
        // logger(NULL, instanceName, fmi2Error, "logError", "An error occurred decoding %s.", configFilename);
        return NULL;
    }

    return s;
}

FMIStatus doStep(
    System* s,
    double  currentCommunicationPoint,
    double  communicationStepSize,
    bool    noSetFMUStatePriorToCurrentPoint) {

    FMIStatus status = FMIOK;
    void* value = malloc(2048);
    if (value == NULL) {
        status = FMIError;
        return status;
    }

    for (size_t i = 0; i < s->nConnections; i++) {

        Connection* k = &(s->connections[i]);
        FMIInstance* m1 = s->components[k->startComponent]->instance;
        FMIInstance* m2 = s->components[k->endComponent]->instance;

        CHECK_STATUS(getVariable(m1, k->type, &(k->startValueReference), value));
        CHECK_STATUS(setVariable(m2, k->type, &(k->endValueReference), value));
    }

    if (s->parallelDoStep) {

        for (size_t i = 0; i < s->nComponents; i++) {
            Component* component = s->components[i];
#ifdef _WIN32
            WaitForSingleObject(component->mutex, INFINITE);
#else
            pthread_mutex_lock(&component->mutex);
#endif
            component->currentCommunicationPoint = currentCommunicationPoint;
            component->communicationStepSize = communicationStepSize;
            component->noSetFMUStatePriorToCurrentPoint = noSetFMUStatePriorToCurrentPoint;
            component->doStep = true;
#ifdef _WIN32
            ReleaseMutex(component->mutex);
#else
            pthread_mutex_unlock(&component->mutex);
#endif
        }

        for (size_t i = 0; i < s->nComponents; i++) {
            Component* component = s->components[i];
            bool waitForThread = true;
            FMIStatus status;

            while (waitForThread) {
#ifdef _WIN32
                WaitForSingleObject(component->mutex, INFINITE);
#else
                pthread_mutex_lock(&component->mutex);
#endif
                waitForThread = component->doStep;
                status = component->status;
#ifdef _WIN32
                ReleaseMutex(component->mutex);
#else
                pthread_mutex_unlock(&component->mutex);
#endif
            }

            CHECK_STATUS(status);
        }

    }
    else {

        for (size_t i = 0; i < s->nComponents; i++) {

            FMIInstance* m = s->components[i]->instance;

            switch (m->fmiMajorVersion) {
            case FMIMajorVersion2:
                CHECK_STATUS(FMI2DoStep(m, currentCommunicationPoint, communicationStepSize, noSetFMUStatePriorToCurrentPoint));
                break;
            case FMIMajorVersion3: ;
                fmi3Boolean eventHandlingNeeded;
                fmi3Boolean terminateSimulation;
                fmi3Boolean earlyReturn;
                fmi3Float64 lastSuccessfulTime;

                CHECK_STATUS(FMI3DoStep(m, currentCommunicationPoint, communicationStepSize, noSetFMUStatePriorToCurrentPoint, &eventHandlingNeeded, &terminateSimulation, &earlyReturn, &lastSuccessfulTime));
                break;
            default:
                break;
            }
        }

    }

END:
    free(value);
    return status;
}

FMIStatus terminateSystem(System* s) {

    FMIStatus status = FMIOK;

    for (size_t i = 0; i < s->nComponents; i++) {

        Component* component = s->components[i];

        switch (component->instance->fmiMajorVersion) {
        case FMIMajorVersion2:
            CHECK_STATUS(FMI2Terminate(component->instance));
            break;
        case FMIMajorVersion3:
            CHECK_STATUS(FMI3Terminate(component->instance));
            break;
        default:
            break;
        }

        if (s->parallelDoStep) {
#ifdef _WIN32
            WaitForSingleObject(component->mutex, INFINITE);
            component->terminate = true;
            ReleaseMutex(component->mutex);
            WaitForSingleObject(component->thread, INFINITE);
#else
            pthread_mutex_lock(&component->mutex);
            component->terminate = true;
            pthread_mutex_unlock(&component->mutex);
            pthread_join(component->thread, NULL);
#endif
        }
    }

END:
    return status;
}

FMIStatus resetSystem(System* s) {

    FMIStatus status = FMIOK;

    s->time = 0;

    for (size_t i = 0; i < s->nComponents; i++) {

        FMIInstance* m = s->components[i]->instance;

        switch (m->fmiMajorVersion) {
        case FMIMajorVersion2:
            CHECK_STATUS(FMI2Reset(m));
            break;
        case FMIMajorVersion3:
            CHECK_STATUS(FMI3Reset(m));
            break;
        default:
            break;
        }
    }
END:
    return status;
}

void freeSystem(System* s) {

    for (size_t i = 0; i < s->nComponents; i++) {

        Component* component = s->components[i];
        FMIInstance* m = component->instance;

        switch (m->fmiMajorVersion) {
        case FMIMajorVersion2:
            FMI2FreeInstance(m);
            break;
        case FMIMajorVersion3:
            FMI3FreeInstance(m);
            break;
        default:
            break;
        }
        FMIFreeInstance(m);
        if (s->parallelDoStep) {
#ifdef _WIN32
            CloseHandle(component->mutex);
            CloseHandle(component->thread);
#else
            pthread_mutex_destroy(&component->mutex);
            pthread_join(component->thread, NULL);
#endif
        }
        free(component);
    }

    free((void *) s->instanceName);
    free(s);
}
