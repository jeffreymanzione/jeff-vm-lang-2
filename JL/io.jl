module io

class FileInternal {
  def new(fn, rw, a, binary) {
    mode = rw.copy()
    if binary {
      mode.append('b')
    }
    if a {
      mode.append('+')
    }
    self.file = File__(fn, mode)
  }
  def rewind() self.file.rewind__()
  def gets() self.file.gets__()
  def getline() self.file.getline__()
  def puts(s) self.file.puts__(s)
  def close() self.file.close__()
}

class FileReader {
  def new(fn, binary) {
    self.fi = FileInternal(fn, 'r', False, binary)
  }
  def rewind() self.fi.rewind()
  def gets(n) self.fi.gets(n)
  def getline() self.fi.getline()
  def getlines() {
    lines = ''
    while (line = self.getline()) {
      lines.extend(line)
    }
    lines
  }
  def close() self.fi.close()
}

class FileWriter {
  def new(fn, append, binary) {
    self.fi = FileInternal(fn, 'w', append, binary)
  }
  def rewind() self.fi.rewind()
  def write(s) self.fi.puts(s)
  def writeln(s) {
    self.fi.puts(s)
    self.fi.puts('\n')
  }
  def close() self.fi.close()
}

self.IN = FileReader('__STDIN__', False, False)
self.OUT = FileWriter('__STDOUT__', True, False)
self.ERROR = FileWriter('__STDERR__', False, False)

def fprint(f, a) {
  f.write(str(a))
}

def fprintln(f, a) {
  f.writeln(str(a))
}

def print(a) {
  fprint(OUT, a)
}

def println(a) {
  fprintln(OUT, a)
}
