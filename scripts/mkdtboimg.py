#! /usr/bin/env python
# Copyright 2017, The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from __future__ import print_function

"""Tool for packing multiple DTB/DTBO files into a single image"""

import argparse
import os
from array import array
from collections import namedtuple
import struct
from sys import stdout
import zlib

class CompressionFormat(object):
    NO_COMPRESSION = 0x00
    ZLIB_COMPRESSION = 0x01
    GZIP_COMPRESSION = 0x02

class DtEntry(object):
    _COMPRESSION_FORMAT_MASK = 0x0f
    REQUIRED_KEYS = ('dt_file', 'dt_size', 'dt_offset', 'id', 'rev', 'flags',
                     'custom0', 'custom1', 'custom2')

    @staticmethod
    def __get_number_or_prop(arg):
        if not arg or arg[0] == '+' or arg[0] == '-':
            raise ValueError('Invalid argument passed to DTImage')
        if arg[0] == '/':
            # TODO(b/XXX): Use pylibfdt to get property value from DT
            raise ValueError('Invalid argument passed to DTImage')
        else:
            base = 10
            if arg.startswith('0x') or arg.startswith('0X'):
                base = 16
            elif arg.startswith('0'):
                base = 8
            return int(arg, base)

    def __init__(self, **kwargs):
        missing_keys = set(self.REQUIRED_KEYS) - set(kwargs)
        if missing_keys:
            raise ValueError('Missing keys in DtEntry constructor: %r' %
                             sorted(missing_keys))

        self.__dt_file = kwargs['dt_file']
        self.__dt_offset = kwargs['dt_offset']
        self.__dt_size = kwargs['dt_size']
        self.__id = self.__get_number_or_prop(kwargs['id'])
        self.__rev = self.__get_number_or_prop(kwargs['rev'])
        self.__flags = self.__get_number_or_prop(kwargs['flags'])
        self.__custom0 = self.__get_number_or_prop(kwargs['custom0'])
        self.__custom1 = self.__get_number_or_prop(kwargs['custom1'])
        self.__custom2 = self.__get_number_or_prop(kwargs['custom2'])

    def __str__(self):
        sb = []
        sb.append('{key:>20} = {value:d}'.format(key='dt_size',
                                                 value=self.__dt_size))
        sb.append('{key:>20} = {value:d}'.format(key='dt_offset',
                                                 value=self.__dt_offset))
        sb.append('{key:>20} = {value:08x}'.format(key='id',
                                                   value=self.__id))
        sb.append('{key:>20} = {value:08x}'.format(key='rev',
                                                   value=self.__rev))
        sb.append('{key:>20} = {value:08x}'.format(key='custom[0]',
                                                   value=self.__flags))
        sb.append('{key:>20} = {value:08x}'.format(key='custom[1]',
                                                   value=self.__custom0))
        sb.append('{key:>20} = {value:08x}'.format(key='custom[2]',
                                                   value=self.__custom1))
        sb.append('{key:>20} = {value:08x}'.format(key='custom[3]',
                                                   value=self.__custom2))
        return '\n'.join(sb)

    def compression_info(self, version):
        if version is 0:
            return CompressionFormat.NO_COMPRESSION
        return self.flags & self._COMPRESSION_FORMAT_MASK

    @property
    def dt_file(self):
        return self.__dt_file

    @property
    def size(self):
        return self.__dt_size

    @size.setter
    def size(self, value):
        self.__dt_size = value

    @property
    def dt_offset(self):
        return self.__dt_offset

    @dt_offset.setter
    def dt_offset(self, value):
        self.__dt_offset = value

    @property
    def image_id(self):
        return self.__id

    @property
    def rev(self):
        return self.__rev

    @property
    def flags(self):
        return self.__flags

    @property
    def custom0(self):
        return self.__custom0

    @property
    def custom1(self):
        return self.__custom1

    @property
    def custom2(self):
        return self.__custom2


class Dtbo(object):

    _DTBO_MAGIC = 0xd7b7ab1e
    _ACPIO_MAGIC = 0x41435049
    _DT_TABLE_HEADER_SIZE = struct.calcsize('>8I')
    _DT_TABLE_HEADER_INTS = 8
    _DT_ENTRY_HEADER_SIZE = struct.calcsize('>8I')
    _DT_ENTRY_HEADER_INTS = 8
    _GZIP_COMPRESSION_WBITS = 31
    _ZLIB_DECOMPRESSION_WBITS = 47

    def _update_dt_table_header(self):
        struct.pack_into('>8I', self.__metadata, 0, self.magic,
                         self.total_size, self.header_size,
                         self.dt_entry_size, self.dt_entry_count,
                         self.dt_entries_offset, self.page_size,
                         self.version)

    def _update_dt_entry_header(self, dt_entry, metadata_offset):
        struct.pack_into('>8I', self.__metadata, metadata_offset, dt_entry.size,
                         dt_entry.dt_offset, dt_entry.image_id, dt_entry.rev,
                         dt_entry.flags, dt_entry.custom0, dt_entry.custom1,
                         dt_entry.custom2)

    def _update_metadata(self):

        self.__metadata = array('c', ' ' * self.__metadata_size)
        metadata_offset = self.header_size
        for dt_entry in self.__dt_entries:
            self._update_dt_entry_header(dt_entry, metadata_offset)
            metadata_offset += self.dt_entry_size
        self._update_dt_table_header()

    def _read_dtbo_header(self, buf):
        (self.magic, self.total_size, self.header_size,
         self.dt_entry_size, self.dt_entry_count, self.dt_entries_offset,
         self.page_size, self.version) = struct.unpack_from('>8I', buf, 0)

        # verify the header
        if self.magic != self._DTBO_MAGIC and self.magic != self._ACPIO_MAGIC:
            raise ValueError('Invalid magic number 0x%x in DTBO/ACPIO file' %
                             (self.magic))

        if self.header_size != self._DT_TABLE_HEADER_SIZE:
            raise ValueError('Invalid header size (%d) in DTBO/ACPIO file' %
                             (self.header_size))

        if self.dt_entry_size != self._DT_ENTRY_HEADER_SIZE:
            raise ValueError('Invalid DT entry header size (%d) in DTBO/ACPIO file' %
                             (self.dt_entry_size))

    def _read_dt_entries_from_metadata(self):

        if self.__dt_entries:
            raise ValueError('DTBO DT entries can be added only once')

        offset = self.dt_entries_offset / 4
        params = {}
        params['dt_file'] = None
        for i in range(0, self.dt_entry_count):
            dt_table_entry = self.__metadata[offset:offset + self._DT_ENTRY_HEADER_INTS]
            params['dt_size'] = dt_table_entry[0]
            params['dt_offset'] = dt_table_entry[1]
            for j in range(2, self._DT_ENTRY_HEADER_INTS):
                params[DtEntry.REQUIRED_KEYS[j + 1]] = str(dt_table_entry[j])
            dt_entry = DtEntry(**params)
            self.__dt_entries.append(dt_entry)
            offset += self._DT_ENTRY_HEADER_INTS

    def _read_dtbo_image(self):
        # First check if we have enough to read the header
        file_size = os.fstat(self.__file.fileno()).st_size
        if file_size < self._DT_TABLE_HEADER_SIZE:
            raise ValueError('Invalid DTBO file')

        self.__file.seek(0)
        buf = self.__file.read(self._DT_TABLE_HEADER_SIZE)
        self._read_dtbo_header(buf)

        self.__metadata_size = (self.header_size +
                                self.dt_entry_count * self.dt_entry_size)
        if file_size < self.__metadata_size:
            raise ValueError('Invalid or truncated DTBO file of size %d expected %d' %
                             file_size, self.__metadata_size)

        num_ints = (self._DT_TABLE_HEADER_INTS +
                    self.dt_entry_count * self._DT_ENTRY_HEADER_INTS)
        if self.dt_entries_offset > self._DT_TABLE_HEADER_SIZE:
            num_ints += (self.dt_entries_offset - self._DT_TABLE_HEADER_SIZE) / 4
        format_str = '>' + str(num_ints) + 'I'
        self.__file.seek(0)
        self.__metadata = struct.unpack(format_str,
                                        self.__file.read(self.__metadata_size))
        self._read_dt_entries_from_metadata()

    def _find_dt_entry_with_same_file(self, dt_entry):

        dt_entry_path = os.path.realpath(dt_entry.dt_file.name)
        for entry in self.__dt_entries:
            entry_path = os.path.realpath(entry.dt_file.name)
            if entry_path == dt_entry_path:
                return entry
        return None

    def __init__(self, file_handle, dt_type='dtb', page_size=None, version=0):

        self.__file = file_handle
        self.__dt_entries = []
        self.__metadata = None
        self.__metadata_size = 0

        # if page_size is given, assume the object is being instantiated to
        # create a DTBO file
        if page_size:
            if dt_type == 'acpi':
                self.magic = self._ACPIO_MAGIC
            else:
                self.magic = self._DTBO_MAGIC
            self.total_size = self._DT_TABLE_HEADER_SIZE
            self.header_size = self._DT_TABLE_HEADER_SIZE
            self.dt_entry_size = self._DT_ENTRY_HEADER_SIZE
            self.dt_entry_count = 0
            self.dt_entries_offset = self._DT_TABLE_HEADER_SIZE
            self.page_size = page_size
            self.version = version
            self.__metadata_size = self._DT_TABLE_HEADER_SIZE
        else:
            self._read_dtbo_image()

    def __str__(self):
        sb = []
        sb.append('dt_table_header:')
        _keys = ('magic', 'total_size', 'header_size', 'dt_entry_size',
                 'dt_entry_count', 'dt_entries_offset', 'page_size', 'version')
        for key in _keys:
            if key == 'magic':
                sb.append('{key:>20} = {value:08x}'.format(key=key,
                                                           value=self.__dict__[key]))
            else:
                sb.append('{key:>20} = {value:d}'.format(key=key,
                                                         value=self.__dict__[key]))
        count = 0
        for dt_entry in self.__dt_entries:
            sb.append('dt_table_entry[{0:d}]:'.format(count))
            sb.append(str(dt_entry))
            count = count + 1
        return '\n'.join(sb)

    @property
    def dt_entries(self):
        return self.__dt_entries

    def compress_dt_entry(self, compression_format, dt_entry_file):
        compress_zlib = zlib.compressobj()  #  zlib
        compress_gzip = zlib.compressobj(zlib.Z_DEFAULT_COMPRESSION,
                                         zlib.DEFLATED, self._GZIP_COMPRESSION_WBITS)  #  gzip
        compression_obj_dict = {
            CompressionFormat.NO_COMPRESSION: None,
            CompressionFormat.ZLIB_COMPRESSION: compress_zlib,
            CompressionFormat.GZIP_COMPRESSION: compress_gzip,
        }

        if compression_format not in compression_obj_dict:
            ValueError("Bad compression format %d" % compression_format)

        if compression_format is CompressionFormat.NO_COMPRESSION:
            dt_entry = dt_entry_file.read()
        else:
            compression_object = compression_obj_dict[compression_format]
            dt_entry_file.seek(0)
            dt_entry = compression_object.compress(dt_entry_file.read())
            dt_entry += compression_object.flush()
        return dt_entry, len(dt_entry)

    def add_dt_entries(self, dt_entries):
        if not dt_entries:
            raise ValueError('Attempted to add empty list of DT entries')

        if self.__dt_entries:
            raise ValueError('DTBO DT entries can be added only once')

        dt_entry_count = len(dt_entries)
        dt_offset = (self.header_size +
                     dt_entry_count * self.dt_entry_size)

        dt_entry_buf = ""
        for dt_entry in dt_entries:
            if not isinstance(dt_entry, DtEntry):
                raise ValueError('Adding invalid DT entry object to DTBO')
            entry = self._find_dt_entry_with_same_file(dt_entry)
            dt_entry_compression_info = dt_entry.compression_info(self.version)
            if entry and (entry.compression_info(self.version)
                          == dt_entry_compression_info):
                dt_entry.dt_offset = entry.dt_offset
                dt_entry.size = entry.size
            else:
                dt_entry.dt_offset = dt_offset
                compressed_entry, dt_entry.size = self.compress_dt_entry(dt_entry_compression_info,
                                                                         dt_entry.dt_file)
                dt_entry_buf += compressed_entry
                dt_offset += dt_entry.size
                self.total_size += dt_entry.size
            self.__dt_entries.append(dt_entry)
            self.dt_entry_count += 1
            self.__metadata_size += self.dt_entry_size
            self.total_size += self.dt_entry_size

        return dt_entry_buf

    def extract_dt_file(self, idx, fout, decompress):
        if idx > self.dt_entry_count:
            raise ValueError('Invalid index %d of DtEntry' % idx)

        size = self.dt_entries[idx].size
        offset = self.dt_entries[idx].dt_offset
        self.__file.seek(offset, 0)
        fout.seek(0)
        compression_format = self.dt_entries[idx].compression_info(self.version)
        if decompress and compression_format:
            if (compression_format == CompressionFormat.ZLIB_COMPRESSION or
                compression_format == CompressionFormat.GZIP_COMPRESSION):
                fout.write(zlib.decompress(self.__file.read(size), self._ZLIB_DECOMPRESSION_WBITS))
            else:
                raise ValueError("Unknown compression format detected")
        else:
            fout.write(self.__file.read(size))

    def commit(self, dt_entry_buf):
        if not self.__file:
            raise ValueError('No file given to write to.')

        if not self.__dt_entries:
            raise ValueError('No DT image files to embed into DTBO image given.')

        self._update_metadata()

        self.__file.seek(0)
        self.__file.write(self.__metadata)
        self.__file.write(dt_entry_buf)
        self.__file.flush()


def parse_dt_entry(global_args, arglist):

    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('dt_file', nargs='?',
                        type=argparse.FileType('rb'),
                        default=None)
    parser.add_argument('--id', type=str, dest='id', action='store',
                        default=global_args.global_id)
    parser.add_argument('--rev', type=str, dest='rev',
                        action='store', default=global_args.global_rev)
    parser.add_argument('--flags', type=str, dest='flags',
                        action='store',
                        default=global_args.global_flags)
    parser.add_argument('--custom0', type=str, dest='custom0',
                        action='store',
                        default=global_args.global_custom0)
    parser.add_argument('--custom1', type=str, dest='custom1',
                        action='store',
                        default=global_args.global_custom1)
    parser.add_argument('--custom2', type=str, dest='custom2',
                        action='store',
                        default=global_args.global_custom2)
    return parser.parse_args(arglist)


def parse_dt_entries(global_args, arg_list):
    dt_entries = []
    img_file_idx = []
    idx = 0
    # find all positional arguments (i.e. DT image file paths)
    for arg in arg_list:
        if not arg.startswith("--"):
            img_file_idx.append(idx)
        idx = idx + 1

    if not img_file_idx:
        raise ValueError('Input DT images must be provided')

    total_images = len(img_file_idx)
    for idx in xrange(total_images):
        start_idx = img_file_idx[idx]
        if idx == total_images - 1:
            argv = arg_list[start_idx:]
        else:
            end_idx = img_file_idx[idx + 1]
            argv = arg_list[start_idx:end_idx]
        args = parse_dt_entry(global_args, argv)
        params = vars(args)
        params['dt_offset'] = 0
        params['dt_size'] = os.fstat(params['dt_file'].fileno()).st_size
        dt_entries.append(DtEntry(**params))

    return dt_entries

def parse_config_option(line, is_global, dt_keys, global_key_types):

    if line.find('=') == -1:
        raise ValueError('Invalid line (%s) in configuration file' % line)

    key, value = (x.strip() for x in line.split('='))
    if is_global and key in global_key_types:
        if global_key_types[key] is int:
            value = int(value)
    elif key not in dt_keys:
        raise ValueError('Invalid option (%s) in configuration file' % key)

    return key, value

def parse_config_file(fin, dt_keys, global_key_types):

    # set all global defaults
    global_args = dict((k, '0') for k in dt_keys)
    global_args['dt_type'] = 'dtb'
    global_args['page_size'] = 2048
    global_args['version'] = 0

    dt_args = []
    found_dt_entry = False
    count = -1
    for line in fin:
        line = line.rstrip()
        if line.lstrip().startswith('#'):
            continue
        comment_idx = line.find('#')
        line = line if comment_idx == -1 else line[0:comment_idx]
        if not line or line.isspace():
            continue
        if line.startswith((' ', '\t')) and not found_dt_entry:
            # This is a global argument
            key, value = parse_config_option(line, True, dt_keys, global_key_types)
            global_args[key] = value
        elif line.find('=') != -1:
            key, value = parse_config_option(line, False, dt_keys, global_key_types)
            dt_args[-1][key] = value
        else:
            found_dt_entry = True
            count += 1
            dt_args.append({})
            dt_args[-1]['filename'] = line.strip()
    return global_args, dt_args

def parse_create_args(arg_list):

    image_arg_index = 0
    for arg in arg_list:
        if not arg.startswith("--"):
            break
        image_arg_index = image_arg_index + 1

    argv = arg_list[0:image_arg_index]
    remainder = arg_list[image_arg_index:]
    parser = argparse.ArgumentParser(prog='create', add_help=False)
    parser.add_argument('--dt_type', type=str, dest='dt_type',
                        action='store', default='dtb')
    parser.add_argument('--page_size', type=int, dest='page_size',
                        action='store', default=2048)
    parser.add_argument('--version', type=int, dest='version',
                        action='store', default=0)
    parser.add_argument('--id', type=str, dest='global_id',
                        action='store', default='0')
    parser.add_argument('--rev', type=str, dest='global_rev',
                        action='store', default='0')
    parser.add_argument('--flags', type=str, dest='global_flags',
                        action='store', default='0')
    parser.add_argument('--custom0', type=str, dest='global_custom0',
                        action='store', default='0')
    parser.add_argument('--custom1', type=str, dest='global_custom1',
                        action='store', default='0')
    parser.add_argument('--custom2', type=str, dest='global_custom2',
                        action='store', default='0')
    args = parser.parse_args(argv)
    return args, remainder

def parse_dump_cmd_args(arglist):

    parser = argparse.ArgumentParser(prog='dump')
    parser.add_argument('--output', '-o', nargs='?',
                        type=argparse.FileType('wb'),
                        dest='outfile',
                        default=stdout)
    parser.add_argument('--dtb', '-b', nargs='?', type=str,
                        dest='dtfilename')
    parser.add_argument('--decompress', action='store_true', dest='decompress')
    return parser.parse_args(arglist)

def parse_config_create_cmd_args(arglist):
    parser = argparse.ArgumentParser(prog='cfg_create')
    parser.add_argument('conf_file', nargs='?',
                        type=argparse.FileType('rb'),
                        default=None)
    cwd = os.getcwd()
    parser.add_argument('--dtb-dir', '-d', nargs='?', type=str,
                        dest='dtbdir', default=cwd)
    return parser.parse_args(arglist)

def create_dtbo_image(fout, argv):

    global_args, remainder = parse_create_args(argv)
    if not remainder:
        raise ValueError('List of dtimages to add to DTBO not provided')
    dt_entries = parse_dt_entries(global_args, remainder)
    dtbo = Dtbo(fout, global_args.dt_type, global_args.page_size, global_args.version)
    dt_entry_buf = dtbo.add_dt_entries(dt_entries)
    dtbo.commit(dt_entry_buf)
    fout.close()

def dump_dtbo_image(fin, argv):
    dtbo = Dtbo(fin)
    args = parse_dump_cmd_args(argv)
    if args.dtfilename:
        num_entries = len(dtbo.dt_entries)
        for idx in range(0, num_entries):
            with open(args.dtfilename + '.{:d}'.format(idx), 'wb') as fout:
                dtbo.extract_dt_file(idx, fout, args.decompress)
    args.outfile.write(str(dtbo) + '\n')
    args.outfile.close()

def create_dtbo_image_from_config(fout, argv):
    args = parse_config_create_cmd_args(argv)
    if not args.conf_file:
        raise ValueError('Configuration file must be provided')

    _DT_KEYS = ('id', 'rev', 'flags', 'custom0', 'custom1', 'custom2')
    _GLOBAL_KEY_TYPES = {'dt_type': str, 'page_size': int, 'version': int}

    global_args, dt_args = parse_config_file(args.conf_file,
                                             _DT_KEYS, _GLOBAL_KEY_TYPES)
    params = {}
    dt_entries = []
    for dt_arg in dt_args:
        filepath = args.dtbdir + os.sep + dt_arg['filename']
        params['dt_file'] = open(filepath, 'rb')
        params['dt_offset'] = 0
        params['dt_size'] = os.fstat(params['dt_file'].fileno()).st_size
        for key in _DT_KEYS:
            if key not in dt_arg:
                params[key] = global_args[key]
            else:
                params[key] = dt_arg[key]
        dt_entries.append(DtEntry(**params))

    # Create and write DTBO file
    dtbo = Dtbo(fout, global_args['dt_type'], global_args['page_size'], global_args['version'])
    dt_entry_buf = dtbo.add_dt_entries(dt_entries)
    dtbo.commit(dt_entry_buf)
    fout.close()

def print_default_usage(progname):
    sb = []
    sb.append('  ' + progname + ' help all')
    sb.append('  ' + progname + ' help <command>\n')
    sb.append('    commands:')
    sb.append('      help, dump, create, cfg_create')
    print('\n'.join(sb))

def print_dump_usage(progname):
    sb = []
    sb.append('  ' + progname + ' dump <image_file> (<option>...)\n')
    sb.append('    options:')
    sb.append('      -o, --output <filename>  Output file name.')
    sb.append('                               Default is output to stdout.')
    sb.append('      -b, --dtb <filename>     Dump dtb/dtbo files from image.')
    sb.append('                               Will output to <filename>.0, <filename>.1, etc.')
    print('\n'.join(sb))

def print_create_usage(progname):
    sb = []
    sb.append('  ' + progname + ' create <image_file> (<global_option>...) (<dtb_file> (<entry_option>...) ...)\n')
    sb.append('    global_options:')
    sb.append('      --dt_type=<type>         Device Tree Type (dtb|acpi). Default: dtb')
    sb.append('      --page_size=<number>     Page size. Default: 2048')
    sb.append('      --version=<number>       DTBO/ACPIO version. Default: 0')
    sb.append('      --id=<number>       The default value to set property id in dt_table_entry. Default: 0')
    sb.append('      --rev=<number>')
    sb.append('      --flags=<number>')
    sb.append('      --custom0=<number>')
    sb.append('      --custom1=<number>')
    sb.append('      --custom2=<number>\n')

    sb.append('      The value could be a number or a DT node path.')
    sb.append('      <number> could be a 32-bits digit or hex value, ex. 68000, 0x6800.')
    sb.append('      <path> format is <full_node_path>:<property_name>, ex. /board/:id,')
    sb.append('      will read the value in given FTB file with the path.')
    print('\n'.join(sb))

def print_cfg_create_usage(progname):
    sb = []
    sb.append('  ' + progname + ' cfg_create <image_file> <config_file> (<option>...)\n')
    sb.append('    options:')
    sb.append('      -d, --dtb-dir <dir>      The path to load dtb files.')
    sb.append('                               Default is load from the current path.')
    print('\n'.join(sb))

def print_usage(cmd, _):
    prog_name = os.path.basename(__file__)
    if not cmd:
        print_default_usage(prog_name)
        return

    HelpCommand = namedtuple('HelpCommand', 'help_cmd, help_func')
    help_commands = (HelpCommand('dump', print_dump_usage),
                     HelpCommand('create', print_create_usage),
                     HelpCommand('cfg_create', print_cfg_create_usage),
                     )

    if cmd == 'all':
        print_default_usage(prog_name)

    for help_cmd, help_func in help_commands:
        if cmd == 'all' or cmd == help_cmd:
            help_func(prog_name)
            if cmd != 'all':
                return

    print('Unsupported help command: %s' % cmd, end='\n\n')
    print_default_usage(prog_name)
    return

def main():

    parser = argparse.ArgumentParser(prog='mkdtboimg.py')

    subparser = parser.add_subparsers(title='subcommand',
                                      description='Valid subcommands')

    create_parser = subparser.add_parser('create', add_help=False)
    create_parser.add_argument('argfile', nargs='?',
                               action='store', help='Output File',
                               type=argparse.FileType('wb'))
    create_parser.set_defaults(func=create_dtbo_image)

    config_parser = subparser.add_parser('cfg_create', add_help=False)
    config_parser.add_argument('argfile', nargs='?',
                               action='store',
                               type=argparse.FileType('wb'))
    config_parser.set_defaults(func=create_dtbo_image_from_config)

    dump_parser = subparser.add_parser('dump', add_help=False)
    dump_parser.add_argument('argfile', nargs='?',
                             action='store',
                             type=argparse.FileType('rb'))
    dump_parser.set_defaults(func=dump_dtbo_image)

    help_parser = subparser.add_parser('help', add_help=False)
    help_parser.add_argument('argfile', nargs='?', action='store')
    help_parser.set_defaults(func=print_usage)

    (subcmd, subcmd_args) = parser.parse_known_args()
    subcmd.func(subcmd.argfile, subcmd_args)

if __name__ == '__main__':
    main()
