code = bytearray()

with open("./build/test.rom", "rb") as in_file:
    code = bytearray(in_file.read())

rom = code + bytearray([0xea] * (32768 - len(code)))

rom[0x7ffc] = 0x00
rom[0x7ffd] = 0x80

with open("./build/testExpanded.rom", "wb") as out_file:
    out_file.write(rom)
