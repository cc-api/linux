=================================
Intel On Demand Netlink interface
=================================

The Intel On Demand driver uses a generic netlink interface for performing
authorization and signed measurement requests using the SPDM protocol. The
family name is ``intel_sdsi``. Userspace applications should use the macros
``INTEL_SDSI_GENL_NAME`` and ``INTEL_SDSI_GENL_VERSION`` which are defined in
``include/uapi/linux/sdsi_nl.h``. General information about the Netlink
userspace API in Linux can be found at
https://docs.kernel.org/userspace-api/netlink/intro.html.

Commands
========

===========================  ==================================
 ``SDSI_GENL_CMD_GET_DEVS``   get list of devices
 ``SDSI_GENL_CMD_GET_INFO``   get device information
 ``SDSI_GENL_CMD_GET_SPDM``   SPDM Requester/Responder exchange
 ===========================  =================================

SDSI_GENL_CMD_GET_DEVS
======================

Requests the device information for all On Demand devices. Response will contain
an array of nested attributes containing the device name and id. The device name
will match the name of the auxiliary device in sysfs. The device id is used with
the ``SDSI_GENL_CMD_GET_SPDM`` command to select the device to on which to
perform an SPDM message exchange

    - no ``NLM_F_DUMP``, with device id: Get device id and device name
    - ``NLM_F_DUMP``, no device: Get array or device id and name for all devices

Userspace request contents:

 +----------------------------------+--------+---------------------------+
 | ``SDSI_GENL_ATTR_DEV_ID``        | u32    | device id                 |
 +----------------------------------+--------+---------------------------+

Kernel response contents:

 +----------------------------------+--------+---------------------------+
 | ``SDSI_GENL_ATTR_DEVS+``         | nested | reply header              |
 +-+--------------------------------+--------+---------------------------+
 | | ``SDSI_GENL_ATTR_DEV_ID``      | u32    | device id                 |
 +-+--------------------------------+--------+---------------------------+
 | | ``SDSI_GENL_ATTR_DEV_NAME``    | string | device name               |
 +-+--------------------------------+--------+---------------------------+

SDSI_GENL_CMD_GET_INFO
======================

Request the maximum size limits for ``SDSI_GENL_ATTR_SPDM_REQ`` and
``SDSI_GENL_ATTR_SPDM_RSP`` for a device.

Userspace request contents:

 +----------------------------------+--------+---------------------------+
 | ``SDSI_GENL_ATTR_DEV_ID``        | u32    | device id                 |
 +----------------------------------+--------+---------------------------+

Kernel response contents:

 +----------------------------------+--------+---------------------------+
 | ``SDSI_GENL_ATTR_SPDM_REQ_SIZE`` | u32    | maximum SPDM request size |
 +----------------------------------+--------+---------------------------+
 | ``SDSI_GENL_ATTR_SPDM_RSP_SIZE`` | u32    | maximum SPDM request size |
 +----------------------------------+--------+---------------------------+

SDSI_GENL_CMD_GET_SPDM
======================

Performs an SPDM message exchange, sending an Requester message in
``SDSI_GENL_ATTR_SPDM_REQ`` and returning the Responder reply in
``SDSI_GENL_ATTR_SPDM_RSP``. The On Demand driver will validate that the request
size is not greater than ``SDSI_GENL_ATTR_SPDM_REQ_SIZE``. The reply size will
not exceed ``SDSI_GENL_ATTR_SPDM_RSP_SIZE``. Requires ``CAP_NET_ADMIN``.

Userspace request contents:

 +-----------------------------+--------+--------------+
 | ``SDSI_GENL_ATTR_DEV_ID``   | u32    | device id    |
 +-----------------------------+--------+--------------+
 | ``SDSI_GENL_ATTR_SPDM_REQ`` | binary | SPDM request |
 +-----------------------------+--------+--------------+

Kernel response contents:

 +-----------------------------+--------+---------------+
 | ``SDSI_GENL_ATTR_DEV_ID``   | u32    | device id     |
 +-----------------------------+--------+---------------+
 | ``SDSI_GENL_ATTR_SPDM_RSP`` | binary | SPDM response |
 +-----------------------------+--------+---------------+

Errors
======

    - -EINVAL: the message is malformed
    - -ENODEV: if the device id cannot be found
    - -EMSGSIZE: general netlink error if the message length exceeds socket length (example: SPDM request message size is greater than ``SDSI_GENL_ATTR_SPDM_REQ_SIZE``)
