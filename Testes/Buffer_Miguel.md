**1. Compilei o código**

C:\\Users\\Usuario\\Documents\\proj2-so>gcc -O2 -Wall -o medir.exe medir.c -lpsapi



**2. Gerei o arquivo de teste (100 MB)**

C:\\Users\\Usuario\\Documents\\proj2-so>.\\medir.exe gerar texto\_100.txt 100 so-texto

texto\_100.txt: 104857625 bytes, 1477525 linhas, fnv1a=51e8d96146c2fc49



**3. Testei com ARRAY CONTÍGUO (3 rep cada)**



**buffer tamanho 4 KB**

C:\\Users\\Usuario\\Documents\\proj2-so>.\\medir.exe texto\_100.txt --estrutura array --leitura read --buffer 4K

windows,array,read,4096,1,0,104857625,1477525,11038148880,3.66,103.68,100.02



C:\\Users\\Usuario\\Documents\\proj2-so>.\\medir.exe texto\_100.txt --estrutura array --leitura read --buffer 4K

windows,array,read,4096,1,0,104857625,1477525,11038148880,3.66,103.68,100.02



C:\\Users\\Usuario\\Documents\\proj2-so>.\\medir.exe texto\_100.txt --estrutura array --leitura read --buffer 4K

windows,array,read,4096,1,0,104857625,1477525,11038148880,3.66,103.68,100.02



**buffer tamanho 64 KB**

C:\\Users\\Usuario\\Documents\\proj2-so>.\\medir.exe texto\_100.txt --estrutura array --leitura read --buffer 64K

windows,array,read,65536,1,0,104857625,1477525,11038148880,3.65,103.67,100.02



C:\\Users\\Usuario\\Documents\\proj2-so>.\\medir.exe texto\_100.txt --estrutura array --leitura read --buffer 64K

windows,array,read,65536,1,0,104857625,1477525,11038148880,3.65,103.67,100.02



C:\\Users\\Usuario\\Documents\\proj2-so>.\\medir.exe texto\_100.txt --estrutura array --leitura read --buffer 64K

windows,array,read,65536,1,0,104857625,1477525,11038148880,3.66,103.68,100.02



**buffer tamanho 1 MB**

C:\\Users\\Usuario\\Documents\\proj2-so>.\\medir.exe texto\_100.txt --estrutura array --leitura read --buffer 1M

windows,array,read,1048576,1,0,104857625,1477525,11038148880,3.66,103.68,100.02



C:\\Users\\Usuario\\Documents\\proj2-so>.\\medir.exe texto\_100.txt --estrutura array --leitura read --buffer 1M

windows,array,read,1048576,1,0,104857625,1477525,11038148880,3.66,103.68,100.02



C:\\Users\\Usuario\\Documents\\proj2-so>.\\medir.exe texto\_100.txt --estrutura array --leitura read --buffer 1M

windows,array,read,1048576,1,0,104857625,1477525,11038148880,3.66,103.68,100.02



**4. Testes com LISTA DE LINHAS**



**buffer tamanho 4 KB**

C:\\Users\\Usuario\\Documents\\proj2-so>.\\medir.exe texto\_100.txt --estrutura lista --leitura read --buffer 4K

windows,lista,read,4096,1,0,104857625,1477525,11038148880,3.65,137.39,133.74



C:\\Users\\Usuario\\Documents\\proj2-so>.\\medir.exe texto\_100.txt --estrutura lista --leitura read --buffer 4K

windows,lista,read,4096,1,0,104857625,1477525,11038148880,3.65,137.39,133.74



C:\\Users\\Usuario\\Documents\\proj2-so>.\\medir.exe texto\_100.txt --estrutura lista --leitura read --buffer 4K

windows,lista,read,4096,1,0,104857625,1477525,11038148880,3.66,137.39,133.74



**buffer tamanho 64 KB**

C:\\Users\\Usuario\\Documents\\proj2-so>.\\medir.exe texto\_100.txt --estrutura lista --leitura read --buffer 64K

windows,lista,read,65536,1,0,104857625,1477525,11038148880,3.65,137.47,133.82



C:\\Users\\Usuario\\Documents\\proj2-so>.\\medir.exe texto\_100.txt --estrutura lista --leitura read --buffer 64K

windows,lista,read,65536,1,0,104857625,1477525,11038148880,3.66,137.50,133.84



C:\\Users\\Usuario\\Documents\\proj2-so>.\\medir.exe texto\_100.txt --estrutura lista --leitura read --buffer 64K

windows,lista,read,65536,1,0,104857625,1477525,11038148880,3.66,137.46,133.80



**buffer tamanho 1 MB**

C:\\Users\\Usuario\\Documents\\proj2-so>.\\medir.exe texto\_100.txt --estrutura lista --leitura read --buffer 1M

windows,lista,read,1048576,1,0,104857625,1477525,11038148880,3.66,138.41,134.75



C:\\Users\\Usuario\\Documents\\proj2-so>.\\medir.exe texto\_100.txt --estrutura lista --leitura read --buffer 1M

windows,lista,read,1048576,1,0,104857625,1477525,11038148880,3.66,138.43,134.78



C:\\Users\\Usuario\\Documents\\proj2-so>.\\medir.exe texto\_100.txt --estrutura lista --leitura read --buffer 1M

windows,lista,read,1048576,1,0,104857625,1477525,11038148880,3.66,138.41,134.75



C:\\Users\\Usuario\\Documents\\proj2-so>

