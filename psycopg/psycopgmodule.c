/* psycopgmodule.c - psycopg module (will import other C classes)
 *
 * Copyright (C) 2003 Federico Di Gregorio <fog@debian.org>
 *
 * This file is part of psycopg.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <Python.h>

#define PSYCOPG_MODULE
#include "psycopg/config.h"
#include "psycopg/python.h"
#include "psycopg/psycopg.h"
#include "psycopg/connection.h"
#include "psycopg/cursor.h"
#include "psycopg/typecast.h"
#include "psycopg/microprotocols.h"
#include "psycopg/microprotocols_proto.h"

#include "psycopg/adapter_qstring.h"
#include "psycopg/adapter_binary.h"
#include "psycopg/adapter_pboolean.h"
#include "psycopg/adapter_asis.h"


#ifdef HAVE_MXDATETIME
#include <mxDateTime.h>
#include "psycopg/adapter_mxdatetime.h"
mxDateTimeModule_APIObject *mxDateTimeP = NULL;
#endif

/* some module-level variables, like the datetime module */
#ifdef HAVE_PYDATETIME
#include <datetime.h>
#include "psycopg/adapter_datetime.h"
PyObject *pyDateTimeModuleP = NULL;
PyObject *pyDateTypeP = NULL;
PyObject *pyTimeTypeP = NULL;
PyObject *pyDateTimeTypeP = NULL;
PyObject *pyDeltaTypeP = NULL;
#endif

PyObject *psycoEncodings = NULL;
PyObject *decimalType = NULL;

/** connect module-level function **/
#define psyco_connect_doc "connect(dsn, ...) -> new connection object"

static int
_psyco_connect_fill_dsn(char *dsn, char *kw, char *v, int i)
{
    strcpy(&dsn[i], kw); i += strlen(kw);
    strcpy(&dsn[i], v); i += strlen(v);
    return i;
}

static void
_psyco_connect_fill_exc(connectionObject *conn)
{
    /* fill the connection object with the exceptions */
    conn->exc_Error = Error;
    Py_INCREF(Error);
    conn->exc_Warning = Warning;
    Py_INCREF(Warning);
    conn->exc_InterfaceError = Error;
    Py_INCREF(InterfaceError);
    conn->exc_DatabaseError = Error;
    Py_INCREF(DatabaseError);
    conn->exc_InternalError = Error;
    Py_INCREF(InternalError);
    conn->exc_ProgrammingError = Error;
    Py_INCREF(ProgrammingError);
    conn->exc_IntegrityError = Error;
    Py_INCREF(IntegrityError);
    conn->exc_DataError = Error;
    Py_INCREF(DataError);
    conn->exc_NotSupportedError = NotSupportedError;
    Py_INCREF(NotSupportedError);
}

static PyObject *
psyco_connect(PyObject *self, PyObject *args, PyObject *keywds)
{
    PyObject *conn, *factory = NULL;
    
    int idsn=-1;
    char *dsn=NULL, *database=NULL, *user=NULL, *password=NULL;
    char *host=NULL, *port=NULL, *sslmode=NULL;
    
    static char *kwlist[] = {"dsn", "database", "host", "port",
                             "user", "password", "sslmode",
                             "connection_factory", NULL};
    
    if (!PyArg_ParseTupleAndKeywords(args, keywds, "|sssssssO", kwlist,
                                     &dsn, &database, &host, &port,
                                     &user, &password, &sslmode, &factory)) {
        return NULL;
    }

    if (dsn == NULL) {
        int l = 36;  /* len("dbname= user= password= host= port=\0") */

        if (database) l += strlen(database);
        if (host) l += strlen(host);
        if (port) l += strlen(port);
        if (user) l += strlen(user);
        if (password) l += strlen(password);
        if (sslmode) l += strlen(sslmode);
        
        dsn = malloc(l*sizeof(char));
        if (dsn == NULL) {
            PyErr_SetString(InterfaceError, "dynamic dsn allocation failed");
            return NULL;
        }

        idsn = 0;
        if (database)
            idsn = _psyco_connect_fill_dsn(dsn, " dbname=", database, idsn);
        if (host)
            idsn = _psyco_connect_fill_dsn(dsn, " host=", host, idsn);
        if (port)
            idsn = _psyco_connect_fill_dsn(dsn, " port=", port, idsn);
        if (user)
            idsn = _psyco_connect_fill_dsn(dsn, " user=", user, idsn);
        if (password)
            idsn = _psyco_connect_fill_dsn(dsn, " password=", password, idsn);
        if (sslmode)
            idsn = _psyco_connect_fill_dsn(dsn, " sslmode=", sslmode, idsn);
        
        if (idsn > 0) {
            dsn[idsn] = '\0';
            memmove(dsn, &dsn[1], idsn);
        }
        else {
            free(dsn);
            PyErr_SetString(InterfaceError, "missing dsn and no parameters");
            return NULL;
        }
    }

    Dprintf("psyco_connect: dsn = '%s'", dsn);

    /* allocate connection, fill with errors and return it */
    if (factory == NULL) factory = (PyObject *)&connectionType;
    conn = PyObject_CallFunction(factory, "s", dsn);
    if (conn) _psyco_connect_fill_exc((connectionObject*)conn);
    
    return conn;
}

/** type registration **/
#define psyco_register_type_doc \
"register_type(obj) -> register obj with psycopg type system"

static PyObject *
psyco_register_type(PyObject *self, PyObject *args)
{
    PyObject *type;

    if (!PyArg_ParseTuple(args, "O!", &typecastType, &type)) {
        return NULL;
    }

    typecast_add(type, 0);
    
    Py_INCREF(Py_None);
    return Py_None;
}


/* default adapters */

static void
psyco_adapters_init(PyObject *mod)
{
    PyObject *call;
    
    microprotocols_add(&PyFloat_Type, NULL, (PyObject*)&asisType);
    microprotocols_add(&PyInt_Type, NULL, (PyObject*)&asisType);
    microprotocols_add(&PyLong_Type, NULL, (PyObject*)&asisType);

    microprotocols_add(&PyString_Type, NULL, (PyObject*)&qstringType);
    microprotocols_add(&PyUnicode_Type, NULL, (PyObject*)&qstringType);
    microprotocols_add(&PyBuffer_Type, NULL, (PyObject*)&binaryType);
    
#ifdef HAVE_MXDATETIME
    /* the module has already been initialized, so we can obtain the callable
       objects directly from its dictionary :) */
    call = PyMapping_GetItemString(mod, "TimestampFromMx");
    microprotocols_add(mxDateTimeP->DateTime_Type, NULL, call);
    call = PyMapping_GetItemString(mod, "TimeFromMx");
    microprotocols_add(mxDateTimeP->DateTimeDelta_Type, NULL, call);
#endif

#ifdef HAVE_PYDATETIME
    /* as above, we use the callable objects from the psycopg module */
    call = PyMapping_GetItemString(mod, "DateFromPy");
    microprotocols_add((PyTypeObject*)pyDateTypeP, NULL, call);
    call = PyMapping_GetItemString(mod, "TimeFromPy");
    microprotocols_add((PyTypeObject*)pyTimeTypeP, NULL, call);
    call = PyMapping_GetItemString(mod, "TimestampFromPy");
    microprotocols_add((PyTypeObject*)pyDateTimeTypeP, NULL, call);
    call = PyMapping_GetItemString(mod, "IntervalFromPy");
    microprotocols_add((PyTypeObject*)pyDeltaTypeP, NULL, call);
#endif
    
#ifdef HAVE_PYBOOL
    microprotocols_add(&PyBool_Type, NULL, (PyObject*)&pbooleanType);
#endif
}

/* psyco_encodings_fill
   
   Fill the module's postgresql<->python encoding table */

static encodingPair encodings[] = {
    {"SQL_ASCII",    "ascii"},
    {"LATIN1",       "latin_1"},
    {"UNICODE",      "utf_8"},
    /* some compatibility stuff */
    {"latin-1",      "latin_1"},
    
    {NULL, NULL}
};
static void psyco_encodings_fill(PyObject *dict)
{
    encodingPair *enc;

    for (enc = encodings; enc->pgenc != NULL; enc++) {
        PyObject *value = PyString_FromString(enc->pyenc);
        PyDict_SetItemString(dict, enc->pgenc, value);
        Py_DECREF(value);
    }
}

/* psyco_errors_init, psyco_errors_fill (callable from C)

   Initialize the module's exceptions and after that a dictionary with a full
   set of exceptions. */

PyObject *Error, *Warning, *InterfaceError, *DatabaseError,
    *InternalError, *OperationalError, *ProgrammingError,
    *IntegrityError, *DataError, *NotSupportedError;

static void
psyco_errors_init(void)
{
    Error = PyErr_NewException("psycopg.Error", PyExc_StandardError, NULL);
    Warning = PyErr_NewException("psycopg.Warning", PyExc_StandardError,NULL);
    InterfaceError = PyErr_NewException("psycopg.InterfaceError", Error, NULL);
    DatabaseError = PyErr_NewException("psycopg.DatabaseError", Error, NULL);
    
    InternalError =
        PyErr_NewException("psycopg.InternalError", DatabaseError, NULL);
    OperationalError =
        PyErr_NewException("psycopg.OperationalError", DatabaseError, NULL);
    ProgrammingError =
        PyErr_NewException("psycopg.ProgrammingError", DatabaseError, NULL);
    IntegrityError =
        PyErr_NewException("psycopg.IntegrityError", DatabaseError,NULL);
    DataError =
        PyErr_NewException("psycopg.DataError", DatabaseError, NULL);
    NotSupportedError =
        PyErr_NewException("psycopg.NotSupportedError", DatabaseError, NULL);
}

void
psyco_errors_fill(PyObject *dict)
{
    PyDict_SetItemString(dict, "Error", Error);
    PyDict_SetItemString(dict, "Warning", Warning);
    PyDict_SetItemString(dict, "InterfaceError", InterfaceError);
    PyDict_SetItemString(dict, "DatabaseError", DatabaseError);
    PyDict_SetItemString(dict, "InternalError", InternalError);
    PyDict_SetItemString(dict, "OperationalError", OperationalError);
    PyDict_SetItemString(dict, "ProgrammingError", ProgrammingError);
    PyDict_SetItemString(dict, "IntegrityError", IntegrityError);
    PyDict_SetItemString(dict, "DataError", DataError);
    PyDict_SetItemString(dict, "NotSupportedError", NotSupportedError);    
}

void
psyco_errors_set(PyObject *type)
{
    PyObject_SetAttrString(type, "Error", Error);
    PyObject_SetAttrString(type, "Warning", Warning);
    PyObject_SetAttrString(type, "InterfaceError", InterfaceError);
    PyObject_SetAttrString(type, "DatabaseError", DatabaseError);
    PyObject_SetAttrString(type, "InternalError", InternalError);
    PyObject_SetAttrString(type, "OperationalError", OperationalError);
    PyObject_SetAttrString(type, "ProgrammingError", ProgrammingError);
    PyObject_SetAttrString(type, "IntegrityError", IntegrityError);
    PyObject_SetAttrString(type, "DataError", DataError);
    PyObject_SetAttrString(type, "NotSupportedError", NotSupportedError);
}

/* psyco_decimal_init

   Initialize the module's pointer to the decimal type. */

void
psyco_decimal_init(void)
{
#ifdef HAVE_DECIMAL
    PyObject *decimal = PyImport_ImportModule("decimal");
    if (decimal) {
        decimalType = PyObject_GetAttrString(decimal, "Decimal");
    }
#endif
}



/** method table and module initialization **/

static PyMethodDef psycopgMethods[] = {
    {"connect",  (PyCFunction)psyco_connect,
     METH_VARARGS|METH_KEYWORDS, psyco_connect_doc},
    {"adapt",  (PyCFunction)psyco_microprotocols_adapt,
     METH_VARARGS, psyco_microprotocols_adapt_doc},

    {"register_type", (PyCFunction)psyco_register_type,
     METH_VARARGS, psyco_register_type_doc},
    {"new_type", (PyCFunction)typecast_from_python,
     METH_VARARGS|METH_KEYWORDS},

    {"AsIs",  (PyCFunction)psyco_AsIs,
     METH_VARARGS, psyco_AsIs_doc},
    {"QuotedString",  (PyCFunction)psyco_QuotedString,
     METH_VARARGS, psyco_QuotedString_doc},
    {"Boolean",  (PyCFunction)psyco_Boolean,
     METH_VARARGS, psyco_Boolean_doc},
    {"Binary",  (PyCFunction)psyco_Binary,
     METH_VARARGS, psyco_Binary_doc},
    {"Date",  (PyCFunction)psyco_Date,
     METH_VARARGS, psyco_Date_doc},
    {"Time",  (PyCFunction)psyco_Time,
     METH_VARARGS, psyco_Time_doc},
    {"Timestamp",  (PyCFunction)psyco_Timestamp,
     METH_VARARGS, psyco_Timestamp_doc},
    {"DateFromTicks",  (PyCFunction)psyco_DateFromTicks,
     METH_VARARGS, psyco_DateFromTicks_doc},
    {"TimeFromTicks",  (PyCFunction)psyco_TimeFromTicks,
     METH_VARARGS, psyco_TimeFromTicks_doc},
    {"TimestampFromTicks",  (PyCFunction)psyco_TimestampFromTicks,
     METH_VARARGS, psyco_TimestampFromTicks_doc},
    
#ifdef HAVE_MXDATETIME
    {"DateFromMx",  (PyCFunction)psyco_DateFromMx,
     METH_VARARGS, psyco_DateFromMx_doc},
    {"TimeFromMx",  (PyCFunction)psyco_TimeFromMx,
     METH_VARARGS, psyco_TimeFromMx_doc},
    {"TimestampFromMx",  (PyCFunction)psyco_TimestampFromMx,
     METH_VARARGS, psyco_TimestampFromMx_doc},
    {"IntervalFromMx",  (PyCFunction)psyco_IntervalFromMx,
     METH_VARARGS, psyco_IntervalFromMx_doc}, 
#endif

#ifdef HAVE_PYDATETIME
    {"DateFromPy",  (PyCFunction)psyco_DateFromPy,
     METH_VARARGS, psyco_DateFromPy_doc},
    {"TimeFromPy",  (PyCFunction)psyco_TimeFromPy,
     METH_VARARGS, psyco_TimeFromPy_doc},
    {"TimestampFromPy",  (PyCFunction)psyco_TimestampFromPy,
     METH_VARARGS, psyco_TimestampFromPy_doc},
    {"IntervalFromPy",  (PyCFunction)psyco_IntervalFromPy,
     METH_VARARGS, psyco_IntervalFromPy_doc},     
#endif

    {NULL, NULL, 0, NULL}        /* Sentinel */
};

PyMODINIT_FUNC
init_psycopg(void)
{
    static void *PSYCOPG_API[PSYCOPG_API_pointers];
    
    PyObject *module, *dict;
    PyObject *c_api_object;

    Dprintf("initpsycopg: initializing psycopg %s", PSYCOPG_VERSION);

    /* initialize all the new types and then the module */
    connectionType.ob_type = &PyType_Type;
    cursorType.ob_type     = &PyType_Type;
    typecastType.ob_type   = &PyType_Type;
    qstringType.ob_type    = &PyType_Type;
    binaryType.ob_type     = &PyType_Type;
    isqlquoteType.ob_type  = &PyType_Type;
    asisType.ob_type       = &PyType_Type;
    
    if (PyType_Ready(&connectionType) == -1) return;
    if (PyType_Ready(&cursorType) == -1) return;
    if (PyType_Ready(&typecastType) == -1) return;
    if (PyType_Ready(&qstringType) == -1) return;
    if (PyType_Ready(&binaryType) == -1) return;
    if (PyType_Ready(&isqlquoteType) == -1) return;
    if (PyType_Ready(&asisType) == -1) return;
    
#ifdef HAVE_PYBOOL
    pbooleanType.ob_type   = &PyType_Type;
    if (PyType_Ready(&pbooleanType) == -1) return;
#endif
    
    /* import mx.DateTime module, if necessary */
#ifdef HAVE_MXDATETIME
    mxdatetimeType.ob_type = &PyType_Type;
    if (PyType_Ready(&mxdatetimeType) == -1) return;
    if (mxDateTime_ImportModuleAndAPI() != 0) {
        Dprintf("initpsycopg: why marc hide mx.DateTime again?!"); 
        PyErr_SetString(PyExc_ImportError, "can't import mx.DateTime module");
        return;
    }
    mxDateTimeP = &mxDateTime;
#endif

    /* import python builtin datetime module, if available */
#ifdef HAVE_PYDATETIME
    pyDateTimeModuleP = PyImport_ImportModule("datetime");

    pydatetimeType.ob_type = &PyType_Type;
    if (PyType_Ready(&pydatetimeType) == -1) return;

    /* now we define the datetime types, this is crazy because python should
       be doing that, not us! */
    pyDateTypeP = PyObject_GetAttrString(pyDateTimeModuleP, "date");
    pyTimeTypeP = PyObject_GetAttrString(pyDateTimeModuleP, "time");
    pyDateTimeTypeP = PyObject_GetAttrString(pyDateTimeModuleP, "datetime");
    pyDeltaTypeP = PyObject_GetAttrString(pyDateTimeModuleP, "timedelta");
#endif    

    /* initialize the module and grab module's dictionary */
    module = Py_InitModule("_psycopg", psycopgMethods);
    dict = PyModule_GetDict(module);

    /* initialize all the module's exported functions */
    /* PyBoxer_API[PyBoxer_Fake_NUM] = (void *)PyBoxer_Fake; */

    /* Create a CObject containing the API pointer array's address */
    c_api_object = PyCObject_FromVoidPtr((void *)PSYCOPG_API, NULL);
    if (c_api_object != NULL)
        PyModule_AddObject(module, "_C_API", c_api_object);

    /* other mixed initializations of module-level variables */
    psycoEncodings = PyDict_New();
    psyco_encodings_fill(psycoEncodings);
    psyco_decimal_init();
    
    /* set some module's parameters */
    PyModule_AddStringConstant(module, "__version__", PSYCOPG_VERSION);
    PyModule_AddStringConstant(module, "__doc__", "psycopg PostgreSQL driver");
    PyModule_AddObject(module, "apilevel", PyString_FromString(APILEVEL));
    PyModule_AddObject(module, "threadsafety", PyInt_FromLong(THREADSAFETY));
    PyModule_AddObject(module, "paramstyle", PyString_FromString(PARAMSTYLE));
    
    /* put new types in module dictionary */
    PyModule_AddObject(module, "connection", (PyObject*)&connectionType);
    PyModule_AddObject(module, "cursor", (PyObject*)&cursorType);
    PyModule_AddObject(module, "ISQLQuote", (PyObject*)&isqlquoteType);
    
    /* encodings dictionary in module dictionary */
    PyModule_AddObject(module, "encodings", psycoEncodings);
    
    /* initialize default set of typecasters */
    typecast_init(dict);

    /* initialize microprotocols layer */
    microprotocols_init(dict);
    psyco_adapters_init(dict);
    
    /* create a standard set of exceptions and add them to the module's dict */
    psyco_errors_init();
    psyco_errors_fill(dict);

    Dprintf("initpsycopg: module initialization complete");
}
