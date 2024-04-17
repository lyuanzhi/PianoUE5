import mido
import os
import numpy
import sys

mid=mido.MidiFile(sys.argv[1])
# mid=mido.MidiFile("MIDI/曹操.mid")

# tempo/1000000/ticks_per_beat
# print(mid.ticks_per_beat)
# for i, track in enumerate(mid.tracks):
#     for msg in track:
#         print(msg)

keys = []
key_offs = []
tempos = []
for k, track in enumerate(mid.tracks):
    sum_time = 0
    for msg in track:
        sum_time += msg.time
        if msg.type == 'note_on':
            if msg.velocity != 0:
                keys.append([msg.note, msg.velocity, msg.time, sum_time])
            else:
                key_offs.append([msg.note, msg.velocity, msg.time, sum_time])
        elif msg.type == 'set_tempo':
            tempos.append([msg.tempo/1000/mid.ticks_per_beat, msg.time, sum_time]) # use ms instead of s

keys = numpy.array(keys)
keys = keys[numpy.argsort(keys[:,3])]
keys = keys.tolist()

key_offs = numpy.array(key_offs)
key_offs = key_offs[numpy.argsort(key_offs[:,3])]
key_offs = key_offs.tolist()

tempos = numpy.array(tempos)
tempos = tempos[numpy.argsort(tempos[:,2])]
tempos = tempos.tolist()
if len(tempos) == 0 or tempos[0][2] != 0:
    tempos.insert(0, [500/mid.ticks_per_beat, 0, 0]) # use ms instead of s

# print("keys", keys)
# print("key_offs", key_offs)
# print("tempos", tempos)

f = open(os.path.dirname(os.path.abspath(__file__)) + "/keys.txt", "w")
f2 = open(os.path.dirname(os.path.abspath(__file__)) + "/velocities.txt", "w")
f3 = open(os.path.dirname(os.path.abspath(__file__)) + "/press_times.txt", "w")
f.write("500\n") # 解决第一个键不弹起的bug
f2.write("500\n") # 解决第一个键不弹起的bug
f3.write("500\n") # 解决第一个键不弹起的bug
m = 0
adjustValue = 0.95
for n, key in enumerate(keys):
    if n == 0:
        f.write("500 ")
        f.write(str(keys[n][0] - 21))
        f2.write(str(keys[n][1]))
        for r, key_off in enumerate(key_offs):
            if keys[n][0] == key_off[0]:
                f3.write(str(round((key_off[3]-keys[n][3])*tempos[m][0]*adjustValue)))
                key_offs.pop(r)
                break
    else:
        if keys[n][3] - keys[n-1][3] == 0:
            f.write(" ")
            f.write(str(keys[n][0] - 21))
            f2.write(" ")
            f2.write(str(keys[n][1]))
            for r, key_off in enumerate(key_offs):
                if keys[n][0] == key_off[0]:
                    f3.write(" ")
                    f3.write(str(round((key_off[3]-keys[n][3])*tempos[m][0]*adjustValue)))
                    key_offs.pop(r)
                    break
        else:
            f.write("\n")
            while m < len(tempos) - 1:
                if keys[n][3] > tempos[m][2] and keys[n][3] <= tempos[m + 1][2]:
                    f.write(str(round((keys[n][3]-keys[n-1][3])*tempos[m][0]*adjustValue)))
                    break
                else:
                    m += 1
            if m == len(tempos) - 1:
                if keys[n][3] > tempos[m][2]:
                    f.write(str(round((keys[n][3]-keys[n-1][3])*tempos[m][0]*adjustValue)))
            f.write(" ")
            f.write(str(keys[n][0] - 21))
            f2.write("\n")
            f2.write(str(keys[n][1]))
            for r, key_off in enumerate(key_offs):
                if keys[n][0] == key_off[0]:
                    f3.write("\n")
                    f3.write(str(round((key_off[3]-keys[n][3])*tempos[m][0]*adjustValue)))
                    key_offs.pop(r)
                    break
f.close()
f2.close()
f3.close()
