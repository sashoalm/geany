import emb

def contextLen():
        return 10

def onKeyPressed(context, s):
        makeUppercase = (
                context == ''
                or context.endswith('\n')
                or context.endswith('. ')
                or context.endswith('! ')
                or context.endswith('? ')
                )

        if makeUppercase:
            s = s.decode('utf-8')
            if s.isupper():
                s = s.lower()
            if s.islower():
                s = s.upper()
            s = s.encode('utf-8')

        emb.AddCharUTF(s)

        # Add a space after a comma or a sentence-end.
        if s == '.' or s == '?' or s == '!' or s == ',':
            emb.AddCharUTF(' ')
