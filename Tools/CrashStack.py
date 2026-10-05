"""Prints a rough call stack from a Windows crash minidump of the game, using the game's .pdb. No debugger needed.
Usage: python Tools/CrashStack.py [dump file]    (default: the newest dump in %LOCALAPPDATA%\\CrashDumps)

It reads the crashed thread's stack memory and names every value that points into the game's code, so the list can include a few stale entries
from earlier calls; the real callers are in there in order, innermost first."""
import ctypes, glob, os, struct, sys
from ctypes import wintypes

path = sys.argv[1] if len(sys.argv) > 1 else max(glob.glob(os.path.join(os.environ['LOCALAPPDATA'], 'CrashDumps', 'Cortex Command*.dmp')), key=os.path.getmtime)
data = open(path, 'rb').read()
print('dump:', path)

signature, version, streamCount, directoryRva = struct.unpack_from('<IIII', data, 0)
assert signature == 0x504D444D, 'not a minidump'
streams = {}
for i in range(streamCount):
	streamType, size, rva = struct.unpack_from('<III', data, directoryRva + i * 12)
	streams[streamType] = (rva, size)

# Modules (stream 4).
modules = []
rva, _ = streams[4]
count, = struct.unpack_from('<I', data, rva)
for i in range(count):
	base, size, checksum, timestamp, nameRva = struct.unpack_from('<QIIII', data, rva + 4 + i * 108)
	length, = struct.unpack_from('<I', data, nameRva)
	modules.append((base, size, data[nameRva + 4:nameRva + 4 + length].decode('utf-16-le')))

# Memory ranges (stream 5: list of ranges; stream 9: 64-bit list).
ranges = []
if 5 in streams:
	rva, _ = streams[5]
	count, = struct.unpack_from('<I', data, rva)
	for i in range(count):
		start, size, dataRva = struct.unpack_from('<QII', data, rva + 4 + i * 16)
		ranges.append((start, size, dataRva))
if 9 in streams:
	rva, _ = streams[9]
	count, baseRva = struct.unpack_from('<QQ', data, rva)
	offset = baseRva
	for i in range(count):
		start, size = struct.unpack_from('<QQ', data, rva + 16 + i * 16)
		ranges.append((start, size, offset))
		offset += size


def read(address, size):
	for start, length, dataRva in ranges:
		if start <= address and address + size <= start + length:
			return data[dataRva + address - start:dataRva + address - start + size]
	return None


# The exception (stream 6) says which thread; the thread list (stream 3) has its stack.
rva, _ = streams[6]
threadId, _, code, flags, record, address = struct.unpack_from('<IIIIQQ', data, rva)
contextSize, contextRva = struct.unpack_from('<II', data, rva + 8 + 152)
print('exception 0x%08X at 0x%X in thread %d' % (code, address, threadId))
rip, = struct.unpack_from('<Q', data, contextRva + 0xF8)
rsp, = struct.unpack_from('<Q', data, contextRva + 0x98)

dbghelp = ctypes.WinDLL('dbghelp')
process = ctypes.c_void_p(0xC0FFEE)
dbghelp.SymSetOptions(0x00000002 | 0x00000010 | 0x00000004)  # undecorate, load lines, deferred
dbghelp.SymInitializeW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR, wintypes.BOOL]
dbghelp.SymLoadModuleExW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, wintypes.LPCWSTR, wintypes.LPCWSTR, ctypes.c_uint64, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD]
dbghelp.SymLoadModuleExW.restype = ctypes.c_uint64
dbghelp.SymFromAddrW.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64), ctypes.c_void_p]
dbghelp.SymGetLineFromAddrW64.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
repo = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
dbghelp.SymInitializeW(process, repo, False)
game = None
for base, size, name in modules:
	if 'cortex command' in name.lower():
		game = (base, size)
		local = os.path.join(repo, os.path.basename(name))
		dbghelp.SymLoadModuleExW(process, None, local if os.path.exists(local) else name, None, base, size, None, 0)


class SymbolInfo(ctypes.Structure):
	_fields_ = [('SizeOfStruct', wintypes.ULONG), ('TypeIndex', wintypes.ULONG), ('Reserved', ctypes.c_uint64 * 2), ('Index', wintypes.ULONG), ('Size', wintypes.ULONG), ('ModBase', ctypes.c_uint64),
	            ('Flags', wintypes.ULONG), ('Value', ctypes.c_uint64), ('Address', ctypes.c_uint64), ('Register', wintypes.ULONG), ('Scope', wintypes.ULONG), ('Tag', wintypes.ULONG),
	            ('NameLen', wintypes.ULONG), ('MaxNameLen', wintypes.ULONG), ('Name', ctypes.c_wchar * 1024)]


class Line(ctypes.Structure):
	_fields_ = [('SizeOfStruct', wintypes.DWORD), ('Key', ctypes.c_void_p), ('LineNumber', wintypes.DWORD), ('FileName', wintypes.LPWSTR), ('Address', ctypes.c_uint64)]


def name(address):
	symbol = SymbolInfo()
	symbol.SizeOfStruct = ctypes.sizeof(SymbolInfo) - 1024 * 2
	symbol.MaxNameLen = 1023
	displacement = ctypes.c_uint64(0)
	if not dbghelp.SymFromAddrW(process, address, ctypes.byref(displacement), ctypes.byref(symbol)):
		return None
	line = Line()
	line.SizeOfStruct = ctypes.sizeof(Line)
	lineDisplacement = wintypes.DWORD(0)
	where = ''
	if dbghelp.SymGetLineFromAddrW64(process, address, ctypes.byref(lineDisplacement), ctypes.byref(line)):
		where = '  (%s:%d)' % (os.path.basename(line.FileName), line.LineNumber)
	return symbol.Name + where


def module(address):
	for base, size, moduleName in modules:
		if base <= address < base + size:
			return os.path.basename(moduleName)
	return '?'


print('at: %s  [%s]' % (name(rip) or hex(rip), module(rip)))
stack = None
for start, length, dataRva in ranges:
	if start <= rsp < start + length:
		stack = data[dataRva + rsp - start:dataRva + length]
shown = 0
last = None
if stack and game:
	for offset in range(0, len(stack) - 8, 8):
		value, = struct.unpack_from('<Q', stack, offset)
		if game[0] <= value < game[0] + game[1]:
			found = name(value)
			if found and found != last:
				print('  ', found)
				last = found
				shown += 1
				if shown >= 40:
					break
