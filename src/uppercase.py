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

        if makeUppercase and not context.endswith('i.e. ') and not context.endswith('e.g. '):
            s = s.decode('utf-8')
            if s.isupper():
                s = s.lower()
            if s.islower():
                s = s.upper()
            s = s.encode('utf-8')

        emb.AddCharUTF(s)

        # Add a space after a comma or a sentence-end.
        if (s == '.' and not context.endswith(' i') and not context.endswith(' e')) or s == '?' or s == '!' or s == ',':
            emb.AddCharUTF(' ')
