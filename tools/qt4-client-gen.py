#!/usr/bin/python
#
# Copyright (C) 2008 Collabora Limited <http://www.collabora.co.uk>
# Copyright (C) 2008 Nokia Corporation
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

from sys import argv
import xml.dom.minidom
from getopt import gnu_getopt

from libtpcodegen import NS_TP, get_descendant_text, get_by_path
from libqt4codegen import binding_from_usage, cxx_identifier_escape, format_docstring, gather_externals, gather_custom_lists, get_qt4_name

class Generator(object):
    def __init__(self, opts):
        try:
            self.group = opts['--group']
            self.headerfile = opts['--headerfile']
            self.implfile = opts['--implfile']
            self.namespace = opts['--namespace']
            self.realinclude = opts['--realinclude']
            self.prettyinclude = opts['--prettyinclude']
            self.typesinclude = opts['--typesinclude']
            self.mainiface = opts.get('--mainiface', None)
            ifacedom = xml.dom.minidom.parse(opts['--ifacexml'])
            specdom = xml.dom.minidom.parse(opts['--specxml'])
        except KeyError, k:
            assert False, 'Missing required parameter %s' % k.args[0]

        self.hs = []
        self.bs = []
        self.ifacenodes = ifacedom.getElementsByTagName('node')
        self.spec, = get_by_path(specdom, "spec")
        self.custom_lists = gather_custom_lists(self.spec)
        self.externals = gather_externals(self.spec)
        self.mainifacename = self.mainiface and self.mainiface.replace('/', '').replace('_', '') + 'Interface'

    def __call__(self):
        # Output info header and includes
        self.h("""\
/*
 * This file contains D-Bus client proxy classes generated by qt4-client-gen.py.
 *
 * This file can be distributed under the same terms as the specification from
 * which it was generated.
 */

#include <QString>
#include <QObject>
#include <QVariant>

#include <QtGlobal>
#include <QtDBus>

#include <%s>

""" % self.typesinclude)

        self.b("""\
#include <%s>

""" % self.realinclude)

        # Begin namespace
        for ns in self.namespace.split('::'):
            self.hb("""\
namespace %s
{
""" % ns)

        # Output interface proxies
        def ifacenodecmp(x, y):
            xname, yname = x.getAttribute('name'), y.getAttribute('name')

            if xname == self.mainiface:
                return -1
            elif yname == self.mainiface:
                return 1
            else:
                return cmp(xname, yname)

        self.ifacenodes.sort(cmp=ifacenodecmp)
        for ifacenode in self.ifacenodes:
            self.do_ifacenode(ifacenode)

        # End namespace
        self.hb(''.join(['}\n' for ns in self.namespace.split('::')]))

        # Write output to files
        open(self.headerfile, 'w').write(''.join(self.hs))
        open(self.implfile, 'w').write(''.join(self.bs))

    def do_ifacenode(self, ifacenode):
        # Extract info
        name = ifacenode.getAttribute('name').replace('/', '').replace('_', '') + 'Interface'
        iface, = get_by_path(ifacenode, 'interface')
        dbusname = iface.getAttribute('name')

        # Begin class, constructors
        self.h("""
class %(name)s : public QDBusAbstractInterface
{
    Q_OBJECT

public:
    static inline const char *staticInterfaceName()
    {
        return "%(dbusname)s";
    }

    %(name)s(
        const QString& serviceName,
        const QString& objectPath,
        QObject* parent = 0
    );

    %(name)s(
        const QDBusConnection& connection,
        const QString& serviceName,
        const QString& objectPath,
        QObject* parent = 0
    );
""" % {'name' : name,
       'dbusname' : dbusname})

        self.b("""
%(name)s::%(name)s(const QString& serviceName, const QString& objectPath, QObject *parent)
    : QDBusAbstractInterface(serviceName, objectPath, staticInterfaceName(), QDBusConnection::sessionBus(), parent)
{
}

%(name)s::%(name)s(const QDBusConnection& connection, const QString& serviceName, const QString& objectPath, QObject *parent)
    : QDBusAbstractInterface(serviceName, objectPath, staticInterfaceName(), connection, parent)
{
}
""" % {'name' : name})

        # Main interface
        mainifacename = self.mainifacename or 'QDBusAbstractInterface'

        if self.mainifacename != name:
            self.h("""
    %(name)s(const %(mainifacename)s& mainInterface);

    %(name)s(const %(mainifacename)s& mainInterface, QObject* parent);
""" % {'name' : name,
       'mainifacename' : mainifacename})

            self.b("""
%(name)s::%(name)s(const %(mainifacename)s& mainInterface)
    : QDBusAbstractInterface(mainInterface.service(), mainInterface.path(), staticInterfaceName(), mainInterface.connection(), mainInterface.parent())
{
}

%(name)s::%(name)s(const %(mainifacename)s& mainInterface, QObject *parent)
    : QDBusAbstractInterface(mainInterface.service(), mainInterface.path(), staticInterfaceName(), mainInterface.connection(), parent)
{
}
""" % {'name' : name,
       'mainifacename' : mainifacename})

        # Properties
        for prop in get_by_path(iface, 'property'):
            # Skip tp:properties
            if not prop.namespaceURI:
                self.do_prop(prop)

        # Close class
        self.h("""\
};
""")

    def do_prop(self, prop):
        name = prop.getAttribute('name')
        qt4name = get_qt4_name(prop)
        access = prop.getAttribute('access')
        gettername = qt4name
        settername = None

        sig = prop.getAttribute('type')
        tptype = prop.getAttributeNS(NS_TP, 'type')
        binding = binding_from_usage(sig, tptype, self.custom_lists, (sig, tptype) in self.externals)

        if 'write' in access:
            settername = set + gettername[0].upper() + gettername[1:]

        self.h("""
    Q_PROPERTY(%(val)s %(qt4name)s READ %(gettername)s%(maybesettername)s)

    inline %(val)s %(gettername)s() const
    {
        return %(getter-return)s;
    }
""" % {'val' : binding.val,
       'qt4name' : qt4name,
       'gettername' : gettername,
       'maybesettername' : settername and (' WRITE ' + settername) or '',
       'getter-return' : 'read' in access and ('qvariant_cast<%s>(internalPropGet("%s"))' % (binding.val, name)) or binding.val + '()'})

        if settername:
            self.h("""
    inline void %s(%s newValue)
    {
        internalPropSet("%s", QVariant::fromValue(newValue));
    }
""" % (settername, binding.inarg, propname))

    def h(self, str):
        self.hs.append(str)

    def b(self, str):
        self.bs.append(str)

    def hb(self, str):
        self.h(str)
        self.b(str)


if __name__ == '__main__':
    options, argv = gnu_getopt(argv[1:], '',
            ['group=',
             'namespace=',
             'headerfile=',
             'implfile=',
             'ifacexml=',
             'specxml=',
             'realinclude=',
             'prettyinclude=',
             'typesinclude=',
             'mainiface='])

    Generator(dict(options))()
