import emb

makeUppercase = False
currentPos = emb.MainCaret()
if currentPos == 0:
        makeUppercase = True
else:
        ch = emb.CharAt(currentPos-1)
        if ch == '\n':
                makeUppercase = True
        elif ch == ' ':
                ch2 = emb.CharAt(currentPos-2)
                if ch2 == '.' or ch2 == '!' or ch2 == '?':
                        makeUppercase = True

if makeUppercase:
    wch = emb.AddedChar()
    if wch.isupper():
        emb.SetAddedChar(wch.lower())
    if wch.islower():
        emb.SetAddedChar(wch.upper())

emb.AddCharUTF_Original()

# Add a space after a comma or a sentence-end.
wch = emb.AddedChar()
print wch
if wch == '.' or wch == '?' or wch == '!' or wch == ',':
    emb.AddCharUTF(' ')
