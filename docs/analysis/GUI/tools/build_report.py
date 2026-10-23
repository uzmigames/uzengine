"""Build an offline reading edition from the report's Markdown sources."""
from pathlib import Path
from html import escape
from html.parser import HTMLParser
from urllib.parse import urlsplit, unquote
import markdown
from markdown.extensions.toc import slugify

BASE = Path(__file__).resolve().parents[1]
CHAPTERS = [BASE/"README.md", *sorted(BASE.glob("[0-9][0-9]-*.md")), BASE/"images/README.md"]
SECTIONS = {p.resolve(): ("overview" if i == 0 else "visual-provenance" if p.parent.name == "images" else p.stem)
            for i, p in enumerate(CHAPTERS)}


class ReportLinks(HTMLParser):
    def __init__(self, source):
        super().__init__(convert_charrefs=False)
        self.source = source
        self.parts = []

    def handle_starttag(self, tag, attrs):
        converted = []
        for key, value in attrs:
            if key in ("href", "src") and value:
                url = urlsplit(value)
                if not url.scheme and not url.netloc:
                    target = (self.source.parent/unquote(url.path)).resolve() if url.path else self.source.resolve()
                    if key == "href" and target in SECTIONS:
                        value = "#"+SECTIONS[target]+("-"+url.fragment if url.fragment else "")
                    elif target == BASE/"index.html":
                        value = "#overview"
                    elif url.path:
                        value = target.relative_to(BASE).as_posix()+("#"+url.fragment if url.fragment else "")
            converted.append(key if value is None else key+'="'+escape(value, quote=True)+'"')
        self.parts.append("<"+tag+(" "+" ".join(converted) if converted else "")+">")

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)

    def handle_endtag(self, tag):
        self.parts.append("</"+tag+">")

    def handle_data(self, data):
        self.parts.append(data)

    def handle_entityref(self, name):
        self.parts.append("&"+name+";")

    def handle_charref(self, name):
        self.parts.append("&#"+name+";")


STYLE = """
:root{--paper:#181818;--ink:#e5e5e5;--muted:#a0a0a0;--line:#3b3b3b;--accent:#efefef}
*{box-sizing:border-box}html{scroll-behavior:auto}body{margin:0;background:var(--paper);color:var(--ink);
font:17px/1.65 Georgia,serif;letter-spacing:0}a{color:var(--accent);text-underline-offset:3px}
aside{position:fixed;inset:0 auto 0 0;width:275px;background:#101010;border-right:1px solid var(--line);
padding:28px 20px;overflow:auto;font:14px/1.6 Bahnschrift,"Segoe UI",sans-serif}
aside strong{display:block;font-size:23px;margin-bottom:20px}aside a{display:block;padding:8px 0}
.brand-mark{display:inline-block;width:28px;height:28px;margin:0 10px 0 0;vertical-align:middle}
main{margin-left:275px;max-width:1540px;padding:38px 48px}article{padding-bottom:70px;
border-bottom:1px solid var(--line);margin-bottom:50px;scroll-margin-top:20px}
h1,h2,h3,h4{font-family:Bahnschrift,"Segoe UI",sans-serif;line-height:1.25;letter-spacing:0}
h1{font-size:34px;margin-top:0}h2{font-size:25px;margin-top:38px}h3{font-size:20px;margin-top:28px}
p,li{max-width:100ch}img{max-width:100%;height:auto;display:block;margin:25px 0}
table{display:block;width:100%;overflow:auto;border-collapse:collapse;margin:25px 0;
font:14px/1.5 Bahnschrift,"Segoe UI",sans-serif}th,td{border:1px solid var(--line);
padding:11px 13px;min-width:130px;vertical-align:top;text-align:left}th{background:#292929}
td{overflow-wrap:anywhere}tr:nth-child(even){background:#202020}
code,pre{font-family:Consolas,monospace;font-size:14px}code{overflow-wrap:anywhere}
pre{background:#242424;padding:18px;overflow:auto}blockquote{border-left:4px solid var(--accent);padding-left:20px}
.source{font:13px/1.5 Bahnschrift,sans-serif;color:var(--muted);margin-top:22px}
@media(max-width:900px){aside{position:static;width:auto;border-right:0;border-bottom:1px solid var(--line)}
aside a{display:inline-block;margin-right:18px}main{margin:0;padding:24px 18px}h1{font-size:28px}}
@media print{:root{--paper:#ffffff;--ink:#202020;--muted:#606060;--line:#cccccc;--accent:#202020}
aside{display:none}main{margin:0;padding:0;max-width:none}article{break-before:page}
th,tr:nth-child(even),pre{background:#f2f2f2}
article:first-child{break-before:auto}table{font-size:10px}th,td{padding:6px;min-width:0}a{color:inherit}}
"""


def main():
    navigation, articles = [], []
    for source in CHAPTERS:
        text = source.read_text(encoding="utf-8")
        title = text.splitlines()[0].lstrip("# ")
        section = SECTIONS[source.resolve()]
        navigation.append('<a href="#'+section+'">'+escape(title)+'</a>')
        converter = markdown.Markdown(extensions=["tables", "fenced_code", "toc"],
                                      extension_configs={"toc":{"slugify":lambda value, sep, s=section: s+"-"+slugify(value, sep)}})
        parser = ReportLinks(source)
        parser.feed(converter.convert(text))
        articles.append('<article id="'+section+'">'+"".join(parser.parts)+
                        '<p class="source">Source: <a href="'+source.relative_to(BASE).as_posix()+'">'+
                        escape(source.name)+'</a></p></article>')
    page = '<!doctype html><html lang="en"><head><meta charset="utf-8">'+\
           '<meta name="viewport" content="width=device-width, initial-scale=1">'+\
           '<link rel="icon" href="images/sources/uzmi-icon.png">'+\
           '<title>UzEngine GUI strategy and project readiness</title><style>'+STYLE+\
           '</style></head><body><aside><strong><img class="brand-mark" src="images/sources/uzmi-icon.png" alt="Uzmi">UzEngine / GUI</strong>'+\
           '<p>Audit and native editor proposal<br>22 October 2026</p>'+\
           "".join(navigation)+'</aside><main>'+"".join(articles)+'</main></body></html>'
    (BASE/"index.html").write_text(page,encoding="utf-8")
    print("Built index.html from", len(CHAPTERS), "Markdown documents; no remote assets or scripts.")


if __name__ == "__main__":
    main()
