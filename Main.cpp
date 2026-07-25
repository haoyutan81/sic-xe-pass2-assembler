#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <iomanip>
#include <algorithm>

using namespace std;

struct RawLine {
    string text;
    int lineNumber;
};
vector<RawLine> raw;

struct sourceline {
    string label;
    string opcode;
    string operand;
    string locctr;
    string addring_mode;
    string format;
    string errorMessage;
    string lineNumber;
};
vector<sourceline> program;   
vector<sourceline> errorsource;

unordered_map<string, string> optab;
unordered_map<string, string> symtab; 

string format2[8]{
    "ADDR","CLEAR","COMPR","DIVR","MULR","RMO","SUBR","TIXR"
};
string pseudocode[8]{
    "START","END","BYTE","WORD","RESB","RESW","EQU","BASE"
};
string onlyOneRegister[3] = {
    "CLEAR",
    "TIXR",
    "SVC"
};
string twoOperandFormat2[8] = {
    "ADDR",
    "SUBR",
    "MULR",
    "DIVR",
    "COMPR",
    "RMO",
    "SHIFTL",
    "SHIFTR"
};

unordered_map<string, string> regtab{
    {"A","0"},{"X","1"},{"L","2"},{"B","3"},
    {"S","4"},{"T","5"},{"F","6"},{"PC","8"},{"SW","9"}
};
 
vector<string> modificationRecords;
vector<string> objectprogram;
bool haveSTART = false;
bool haveEND = false;
bool _error=false;

//  PASS 1 
int calcIncrement(const string& rawOpcode, const string& operand, const string& format) {
    if (rawOpcode == "START" || rawOpcode == "END" ||
        rawOpcode == "EQU"   || rawOpcode == "BASE") return 0;
    if (format == "2") return 2;
    if (format == "3") return 3;
    if (format == "4") return 4;
    if (rawOpcode == "WORD") return 3;
    if (rawOpcode == "RESB") return stoi(operand);
    if (rawOpcode == "RESW") return stoi(operand) * 3;
    if (rawOpcode == "BYTE") {
        if (operand.size() >= 2 && operand[0] == 'C') return (int)operand.size() - 3;
        if (operand.size() >= 2 && operand[0] == 'X') return ((int)operand.size() - 3 + 1) / 2;
    }
    return 0;
}

void getopcode() {
    ifstream fin("opcode.txt");
    string line;
    while (getline(fin, line)) {
        string mnemonic, opcode;
        stringstream ss(line);
        ss >> mnemonic >> opcode;
        optab[mnemonic] = opcode;
    }
}

void cleantxt(string file) {
    ifstream fin(file);
    string line;
    int lineNo = 0;
    while (getline(fin, line)) {
        lineNo++;
        
        // 1. 先過濾真正的空行或純註解行
        if (line.empty() || line[0] == '.') continue;
        
        // 2. 去除行尾的 . 註解
        size_t commentpos = line.find('.');
        if (commentpos != string::npos) line = line.substr(0, commentpos);
        
        // 3. 去除行首的空格 (Ltrim)
        size_t commentspase = line.find_first_not_of(' ');
        if (commentspase != string::npos) {
            line = line.substr(commentspase);
        } else {
            // 【新增修改】如果找不到非空格字元，代表整行消完註解後只剩空格
            continue; 
        }

        // 4. 【新增安全檢查】去除空格後，如果行尾剛好有 . 導致切完變空字串，也該忽略
        if (line.empty()) continue; 

        raw.push_back({line, lineNo});
    }
    fin.close();
}


string getaddressmode(string format, string& operand, string opcode) {
    if (format == "3" || format == "4" || format == "2" || format == "") {
        if (opcode == "RSUB")          return "SIMPLE";
        if (operand.empty() || format == "2" || format == "") return "";
        if (operand[0] == '#'){
            operand=operand.substr(1);
            return "IMMEDIATE";
        }
        if (operand[0] == '@'){
            operand=operand.substr(1);
            return "INDIRECT";
        }
        if (operand.find(",X") != string::npos){
            operand=operand.substr(0,operand.size()-2);
            return "INDEXED";
        }
    }
    return "SIMPLE";
}

string formatclassify(string &opcode) {
    if (opcode.front() == '+') {
        opcode = opcode.substr(1);  // 移除 '+' 前綴
        return "4";
    }
    for (const string& p : format2)    if (p == opcode) return "2";
    for (const string& p : pseudocode) if (p == opcode) return "";
    return "3";
}

string removespace(string line) {
    if (line.find("BYTE") != string::npos) return line;
    size_t pos = 0;
    while ((pos = line.find(',', pos)) != string::npos) {
        while (pos > 0 && line[pos - 1] == ' ') { line.erase(pos - 1, 1); pos--; }
        while (pos + 1 < line.size() && line[pos + 1] == ' ') line.erase(pos + 1, 1);
        pos++;
    }
    return line;
}

bool isopcode(const string& s) {
    string base = (!s.empty() && s[0] == '+') ? s.substr(1) : s;
    for (const string& p : pseudocode) if (p == base) return true;
    for (const string& p : format2)    if (p == base) return true;
    if (optab.find(base) != optab.end()) return true;
    return false;
}

bool label_has_error(const string& label, sourceline& temp) {
    if (!label.empty() && symtab.find(label) != symtab.end()) {
        temp.errorMessage = "DUPLICATE_LABEL";
        return true;
    }
    if(optab.find(label) != optab.end()){
        temp.errorMessage = "LABEL_CANNOT_BE_A_MNEMONIC";
        return true;
    }
    if(!label.empty() && !temp.opcode.empty() && !temp.operand.empty()  && label==temp.operand){
        temp.errorMessage="LABEL_OPERAND_COLLISION";
        return true;
    }   
    return false;
}

bool opcode_has_error(const string& opcode, sourceline& temp) {
    string base = (!opcode.empty() && opcode.front() == '+') ? opcode.substr(1) : opcode;
    if (base == "START" && haveSTART) { temp.errorMessage = "MULTIPLE_START"; return true; }
    for (const string& p : pseudocode) if (p == base) return false;
    if (optab.find(base) == optab.end()) {  
        temp.errorMessage = "INVALID_OPCODE";
        return true; }
    if (temp.format == "2" && !opcode.empty() && opcode[0] == '+') {
        temp.errorMessage = "INVALID_FORMAT2_EXTENSION"; return true;
    }
    return false;
}

bool operand_has_error(const string& operand, sourceline& temp) {
    int count = 0;
    if (!operand.empty() && temp.opcode != "BYTE") {
        for (char c : operand) 
            if (c == ',') 
            count++;
        if (count > 1) 
        { temp.errorMessage = "TOO_MANY_COMMA"; return true; }
        
    }
    if (operand.size() >= 3 && operand[0] == 'X' && operand[1] == '\'' && operand.back() == '\''){
        string hexstr = operand.substr(2, operand.size() - 3);

        if(hexstr.size() % 2 != 0)
        {
            temp.errorMessage = "ODD_LENGTH_HEX_STRING";
            return true;
        }

        for(char c : hexstr)
        {
            if(!isxdigit(static_cast<unsigned char>(c)))
            {
                temp.errorMessage = "INVALID_HEX_CONSTANT";
                return true;
            }
        }
    }
    if (temp.opcode == "RSUB" && !operand.empty()) { temp.errorMessage = "RSUB_WITH_OPERAND"; return true; }
    if (temp.opcode == "END" && !operand.empty()) {
        if (symtab.find(operand) == symtab.end()) { temp.errorMessage = "UNDEFINED_SYMBOL"; return true; }
    }
    if (temp.opcode == pseudocode[3] || temp.opcode == pseudocode[4] || temp.opcode == pseudocode[5]) {
        for (char c : operand)
            if (!isdigit(c)) { temp.errorMessage = "OPERAND UNDECIMAL"; return true; }
    }
    if (temp.format == "2" && operand.empty()) { temp.errorMessage = "MISSING_OPERAND"; return true; }
    if (temp.format == "3" || temp.format == "4") {
        if (temp.opcode != "RSUB" && temp.operand.empty())
            temp.errorMessage = "OPERAND_NULL";
    }
    if (temp.format == "3" || temp.format == "4")
    {
        size_t pos = operand.find(',');

        if (pos != string::npos)
        {
            string indexReg = operand.substr(pos + 1);

            auto it = regtab.find(indexReg);

            if (it == regtab.end())
            {
                temp.errorMessage = "INVALID_REGISTER";
                return true;
            }

            if (indexReg != "X")
            {
                temp.errorMessage = "INDEX_REGISTER_NOT_RIGTH";
                return true;
            }
        }
    }
    return false;
}

void parseLine() {
    int locctr = 0;
    for (size_t i = 0; i < raw.size(); i++) {
        vector<string> token;
        sourceline temp;
        string box;
        string line     = removespace(raw[i].text);
        string strnumber = to_string(raw[i].lineNumber);

        size_t isbyte = line.find("BYTE");
        size_t q1     = line.find('\'');
        size_t q2     = line.rfind('\'');

        if (isbyte != string::npos && q1 != string::npos && q1 != q2)
            {
                string before = line.substr(0, q1);

                stringstream ss(before);

                while (ss >> box)
                    token.push_back(box);

                string prefix = token.back();
                token.pop_back();

                string content =
                    line.substr(q1 + 1,
                                q2 - q1 - 1);

                if(prefix != "X" && prefix != "C")
                {
                    temp.errorMessage = "INVALID_BYTE_FORMAT";
                    _error=true;
                    
                }

                else if(content.empty())
                {
                    temp.errorMessage = "BYTE_NULL";
                    _error=true;
                   
                }
                token.push_back(
                    prefix + "'" +
                    content +
                    "'"
                );
            }
            else
            {
                stringstream ss(line);

                while(ss >> box)
                    token.push_back(box);
            }
            if (token.empty()) 
                continue;

            string rawOpcode;
            string rawOperand;
            if (token.size() == 3) {
                bool isMissingComma = 
                find(begin(twoOperandFormat2), end(twoOperandFormat2), token[0]) != end(twoOperandFormat2)
                && regtab.find(token[1]) != regtab.end()
                && regtab.find(token[2]) != regtab.end();

            if (isMissingComma) {
                temp.errorMessage = "REGISTER_SEPARATED_BY_SPACE";
                temp.lineNumber   = strnumber;
                temp.opcode       = token[0];
                temp.operand      = token[1] + " " + token[2];
                errorsource.push_back(temp);
                _error = true;
                continue;
            }

                temp.label        = token[0];
                rawOpcode         = token[1];
                temp.format       = formatclassify(token[1]);
                temp.opcode       = token[1];
                rawOperand        = token[2];
                temp.addring_mode = getaddressmode(temp.format, token[2], temp.opcode);
                temp.operand      = token[2];
                temp.lineNumber   = strnumber;
            } else if (token.size() == 2) {
                if (isopcode(token[0])) {
                    temp.label        = "";
                    rawOpcode         = token[0];
                    temp.format       = formatclassify(token[0]);
                    temp.opcode       = token[0];
                    rawOperand        = token[1];
                    temp.addring_mode = getaddressmode(temp.format, token[1], temp.opcode);
                    temp.operand      = token[1];
                } else {
                    temp.label        = token[0];
                    rawOpcode         = token[1];
                    temp.format       = formatclassify(token[1]);
                    temp.opcode       = token[1];
                    temp.operand      = "";
                }
                temp.lineNumber = strnumber;
            } else if (token.size() == 1) {
                temp.label        = "";
                rawOpcode         = token[0];
                temp.format       = formatclassify(token[0]);
                temp.opcode       = token[0];
                temp.operand      = "";
                temp.lineNumber   = strnumber;
            } else{
                temp.errorMessage = "FORMAT_ERROR";
                temp.lineNumber   = strnumber;
                errorsource.push_back(temp);
                _error = true;
                continue;
            }

        if (temp.opcode == "EQU") {
            if (temp.operand == "*") {
                symtab[temp.label] = temp.locctr;
            } else {
                auto it = symtab.find(temp.operand);
                symtab[temp.label] = (it != symtab.end()) ? it->second : to_string(stoi(temp.operand));
            }
        }

        stringstream hexss;
        hexss << uppercase << hex << setw(4) << setfill('0') << locctr;
        temp.locctr = hexss.str();
        
        
            

        if(opcode_has_error(temp.opcode, temp) ||
        label_has_error(temp.label, temp) ||
        operand_has_error(rawOperand, temp)){
            temp.opcode=rawOpcode;
            temp.operand=rawOperand;
            _error=true;
        }
        if (i == 0) {
    if (temp.opcode == "START") {
        haveSTART = true;
        bool validHex = true;
        for (char c : temp.operand) {
            if (!isxdigit(c)) {
                validHex = false;
                break;
            }
        }
        if (!validHex) {
            temp.errorMessage = "START_NOT_HEX_VALUE";
            _error = true;
        } else {
            locctr = stoi(temp.operand, nullptr, 16);
        }
    } else {
        temp.errorMessage = "FIRST_LINE_HAS_NO_START";
        errorsource.push_back(temp);
        _error = true;
        break;
    }
} else {
    locctr += calcIncrement(temp.opcode, temp.operand, temp.format);
}

        if(!temp.errorMessage.empty()){
            errorsource.push_back(temp);
            }
        else{
            program.push_back(temp);
        }
     
        if (!temp.errorMessage.empty() && haveSTART) 
            continue;
        if (temp.label != "") 
            symtab[temp.label] = temp.locctr;

        if(haveEND){
            temp.errorMessage = "TOO_MANY_END";
            errorsource.push_back(temp);
            _error = true;
            break;
        }
        else if (i == raw.size() - 1 && temp.opcode != "END") {
            temp.lineNumber  = to_string(raw.back().lineNumber);  
            temp.errorMessage = "FINAL_LINE_HAS_NO_END";
            errorsource.push_back(temp);
            _error = true;
        }
        if(temp.opcode=="END"){
            haveEND=true;
        }

    }
}

void outputtxt() {
    ofstream fout("intermediate_file.txt");
    for (auto& line : program) {
        fout << line.lineNumber << " "
             << line.locctr << " "
             << (line.label.empty()        ? "***" : line.label)        << " "
             << (line.opcode.empty()       ? "***" : line.opcode)       << " ";
        if (line.opcode == "BYTE")
            fout << "\"" << (line.operand.empty() ? "***" : line.operand) << "\" ";
        else
            fout << (line.operand.empty()  ? "***" : line.operand)  << " ";
        fout << (line.addring_mode.empty() ? "***" : line.addring_mode) << " "
             << (line.format.empty()       ? "***" : line.format)       << " ";
        fout << "\n";
    }
    fout << "\n";
    for (auto& s : symtab)
        fout << s.first << " " << s.second << "\n";
    fout.close();
}

void printProgram() {
    cout << "\n===== Program (Pass 1) =====\n";
    const int W1=10, W2=10, W3=10, W4=12, W5=18, W6=10, W7=20;
    cout << left
         << setw(W1) << "LineNo"
         << setw(W2) << "Locctr"
         << setw(W3) << "Label"
         << setw(W4) << "Opcode"
         << setw(W5) << "Operand"
         << setw(W6) << "Mode"
         << setw(W7) << "Format"
         << "Error\n"
         << string(W1+W2+W3+W4+W5+W6+W7+10, '-') << "\n";
    for (const auto& ln : program)
        cout << left
             << setw(W1) << ln.lineNumber
             << setw(W2) << ln.locctr
             << setw(W3) << (ln.label.empty()        ? " " : ln.label)
             << setw(W4) << ln.opcode
             << setw(W5) << ln.operand
             << setw(W6) << (ln.addring_mode.empty() ? " " : ln.addring_mode)
             << setw(W7) << ln.format << "\n";
}

void printsymtab() {
    cout << "\n===== symtab =====\n";
    for (const auto& s : symtab)
        cout << s.first << "\t" << s.second << "\n";
}

//  PASS 2
string addzero(int value) {
    stringstream ss;
    ss << uppercase << hex << setfill('0') << setw(6) << value;
    return ss.str();
}

string findopcode(const string& mnemonic) {
    string key = (!mnemonic.empty() && mnemonic[0] == '+') ? mnemonic.substr(1) : mnemonic;
    auto it = optab.find(key);
    return (it != optab.end()) ? it->second : "";
}

int findbase() {
    for (const auto& ln : program) {
        if (ln.opcode == "BASE") {
            auto it = symtab.find(ln.operand);
            if (it != symtab.end()) return stoi(it->second, nullptr, 16);
        }
    }
    return -1;
}

void Hrecord(string name, string start, string end) {
    int startaddr = stoi(start, nullptr, 16);  // 統一 hex
    int endaddr   = stoi(end,   nullptr, 16);  // 統一 hex
    stringstream ss;
    ss << left << setw(6) << setfill(' ') << name ;
    
    objectprogram.push_back("H " + ss.str() + " " + addzero(startaddr) + " " + addzero(endaddr - startaddr));
}

void Trecord(const string& start, int length, const string& object) {
    int startaddr = stoi(start, nullptr, 16);
    stringstream ssStart, ssLen;
    ssStart << uppercase << hex << setw(6) << setfill('0') << startaddr;
    ssLen   << uppercase << hex << setw(2) << setfill('0') << length;
    objectprogram.push_back("T " + ssStart.str() + " " + ssLen.str() + " " + object);
}

string eRecord = "";
void Erecord(string start) {
    eRecord = "E " + addzero(stoi(start, nullptr, 16));
}

void Mrecord(string locctr) {
    modificationRecords.push_back("M " + addzero(stoi(locctr, nullptr, 16) + 1) + " 05");
}

void flushT(string& tStart, int& tLen, string& tObject) {
    if (!tObject.empty()) {
        Trecord(tStart, tLen, tObject);
        tStart.clear(); tObject.clear(); tLen = 0;
    }
}

bool format2registerlimit(const string& opcode,const string& reg1,const string& reg2)
{
    // reg1 必須存在
    if(regtab.find(reg1) == regtab.end())
        return false;

    // 只允許一個 register 的指令
    for(const auto& op : onlyOneRegister)
    {
        if(opcode == op)
        {
            if(!reg2.empty())
                return false;

            return true;
        }
    }

    // 必須兩個 operand 的指令
    for(const auto& op : twoOperandFormat2)
    {
        if(opcode == op)
        {
            if(reg2.empty())
                return false;

            if(regtab.find(reg2) == regtab.end())
                return false;

            return true;
        }
    }

    return true;
}

string generateobjectcode(sourceline& line, size_t num) {
    string value, reg1, reg2, index;
    string box[2];
    if (line.opcode == "START")                  return "";
    if (line.opcode == "END")                    return "";
    if (line.opcode == "EQU")                    return "";
    if (line.opcode == pseudocode[7])            return ""; // BASE
    if (line.opcode == pseudocode[5])            return ""; // RESW
    if (line.opcode == pseudocode[4])            return ""; // RESB
    if (line.opcode == pseudocode[2]) { // BYTE
        if (line.operand[0] == 'C') {
            value = line.operand.substr(2, line.operand.size() - 3);
            for (char c : value) {
                stringstream ss;
                ss << uppercase << hex << setw(2) << setfill('0') << (int)c;
                index += ss.str();
            }
            return index;
        }
        if (line.operand[0] == 'X') {
            value = line.operand.substr(2, line.operand.size() - 3);
            return value;
        }
    }

    if (line.opcode == pseudocode[3]) { // WORD
        stringstream ss;
        ss << uppercase << hex << setw(6) << setfill('0') << stoi(line.operand);
        return ss.str();
    }

    if (line.format == "2")
    {
        value = findopcode(line.opcode);

        size_t pos = line.operand.find(',');
        if (pos != string::npos)
        {
            reg1 = line.operand.substr(0, pos);
            reg2 = line.operand.substr(pos + 1);
        }
        else
        {
            reg1 = line.operand;
        }

        if (!format2registerlimit(line.opcode, reg1, reg2))
        {
             _error=true;
            line.errorMessage = "FORMAT2_REGISTER_ERROR ";
            errorsource.push_back(line);
            return "";
        }

        auto it1 = regtab.find(reg1);

        box[0] = it1->second;

        if (!reg2.empty())
        {
            auto it2 = regtab.find(reg2);
            box[1] = it2->second;
        }
        else
        {
            box[1] = "0";
        }

        return value + box[0] + box[1];
    }
    if (line.format == "3") {
        int n=0, i=0, x=0, b=0, p=0, e=0;
        if (line.addring_mode == "INDIRECT")  { n=1; i=0; }
        if (line.addring_mode == "IMMEDIATE") { n=0; i=1; }
        if (line.addring_mode == "SIMPLE")    { n=1; i=1; }
        if (line.addring_mode == "INDEXED")   { n=1; i=1; x=1; }

        value = findopcode(line.opcode);
        int disp = 0;
        string sym = line.operand;
        auto it = symtab.find(sym);
        if (it != symtab.end()) {
            int target = stoi(it->second, nullptr, 16);
            if (num + 1 >= program.size()) { 
                _error=true;
                line.errorMessage += "INVALID_PC "; 
                return ""; 
            }
            int pc = stoi(program[num + 1].locctr, nullptr, 16);
            disp = target - pc;
            if (disp >= -2048 && disp <= 2047) {
                p = 1; b = 0;
            } else {
                int base = findbase();
                if (base < 0) { 
                    _error=true;
                    line.errorMessage += "DISP_OUT_OF_RANGE "; 
                    errorsource.push_back(line);
                    return ""; }
                disp = target - base;
        
                if (disp < 0 || disp > 4095) { 
                    cout << disp;
                    _error=true;
                    line.errorMessage += "DISP_OUT_OF_RANGE "; 
                    errorsource.push_back(line);
                    return ""; }
                b = 1; p = 0;
            }
        } else {
            if (line.addring_mode == "IMMEDIATE") {
                disp = stoi(sym);
                if (disp < 0 || disp > 4095) { 
                    _error=true;
                    line.errorMessage += "IMMEDIATE_OUT_OF_RANGE "; 
                    errorsource.push_back(line);
                    return ""; }
                p = 0; b = 0;
            } else if (line.opcode == "RSUB"){
                return "4F0000";
            } 
            else{

                _error=true;
                line.errorMessage="UNDEFINED_SYMBOL";
                errorsource.push_back(line);
                return "";
            }
        }

        int opcode = stoi(value, nullptr, 16);
        opcode = (opcode & 0xFC) | (n << 1) | i;
        int xbpe = (x << 3) | (b << 2) | (p << 1) | e;
        if (disp < 0) disp &= 0xFFF;
        int objectCode = (opcode << 16) | (xbpe << 12) | (disp & 0xFFF);

        stringstream ss;
        ss << uppercase << hex << setw(6) << setfill('0') << objectCode;
        return ss.str();
    }

    if (line.format == "4") {
        int n=0, i=0, x=0, b=0, p=0, e=1;
        if (line.addring_mode == "INDIRECT")  { n=1; i=0; }
        if (line.addring_mode == "IMMEDIATE") { n=0; i=1; }
        if (line.addring_mode == "SIMPLE")    { n=1; i=1; }
        if (line.addring_mode == "INDEXED")   { n=1; i=1; x=1; }

        int disp = 0;
        string sym = line.operand;
        if (!sym.empty() && (sym[0] == '#' || sym[0] == '@')) sym = sym.substr(1);
        if (sym.size() > 2 && sym.substr(sym.size() - 2) == ",X") sym = sym.substr(0, sym.size() - 2);

        auto it = symtab.find(sym);
        if (it != symtab.end()) {
            disp = stoi(it->second, nullptr, 16);
            Mrecord(line.locctr);
        } else {
            if (line.addring_mode == "IMMEDIATE") disp = stoi(sym);
            else { 
                _error=true;
                line.errorMessage += "UNDEFINED_SYMBOL "; 
                errorsource.push_back(line);
                return ""; }
        }

        value = findopcode(line.opcode);
        int opcode = stoi(value, nullptr, 16);
        opcode = (opcode & 0xFC) | (n << 1) | i;
        int xbpe = (x << 3) | (b << 2) | (p << 1) | e;
        unsigned int objectCode =
            ((unsigned int)(opcode & 0xFF) << 24) |
            ((unsigned int)(xbpe   & 0x0F) << 20) |
            ((unsigned int) disp   & 0xFFFFF);

        stringstream ss;
        ss << uppercase << hex << setw(8) << setfill('0') << objectCode;
        return ss.str();
    }
    
    
    return "";
}

void runpass2() {
    string tStart = "", tObject = "";
    int tLen = 0;
    for (size_t i = 0; i < program.size(); i++) {
        auto& line = program[i];

        if (line.opcode == "START" && line.errorMessage == "") {
            Hrecord(line.label, line.operand, program[program.size()-1].locctr);
        } else if (i == 0) {
            Hrecord("COPY", "0", program[program.size() - 1].locctr);
        }

        if (line.opcode == "START") continue;
        if (line.opcode == "END")   { flushT(tStart, tLen, tObject); break; }
        if (line.opcode == "RESB" || line.opcode == "RESW") { flushT(tStart, tLen, tObject); continue; }
        if (line.opcode == "BASE"  || line.opcode == "EQU") continue;

        string obj = generateobjectcode(line, i);
        if (obj.empty()) continue;  // 有錯或不產生 object code 的指令

        int objBytes = (int)obj.size() / 2;
        if (tStart.empty()) tStart = line.locctr;

        if (line.opcode == "BYTE") {
            size_t offset = 0;
            // 計算此 BYTE 指令的起始實際地址
            int baseAddr = stoi(line.locctr, nullptr, 16);
            
            while (offset < obj.size()) {
                // 若 tStart 是空的（剛 flush 過），設定正確起始位址
                if (tStart.empty()) {
                    int curAddr = baseAddr + (int)(offset / 2);
                    stringstream ss;
                    ss << uppercase << hex << setw(6) << setfill('0') << curAddr;
                    tStart = ss.str();
                }

                int remain = 30 - tLen;
                if (remain <= 0) {
                    flushT(tStart, tLen, tObject);
                    // flushT 應清空 tStart、tLen、tObject
                    // flush 後 tStart 為空，下一次迴圈開頭會重新設定
                    continue; // 重新計算 remain
                }

                int take = min(remain, (int)(obj.size() - offset) / 2) * 2;
                tObject += (tObject.empty() ? "" : " ") + obj.substr(offset, take);
                tLen    += take / 2;
                offset  += take;
            }
        } else {
            if (tLen + objBytes > 30) {
                flushT(tStart, tLen, tObject);
                tStart = line.locctr;
            }
            tObject += (tObject.empty() ? "" : " ") + obj;
            tLen    += objBytes;
        }
        if(!line.errorMessage.empty()){
            errorsource.push_back(line);
        }
          
    }

    if (_error) {
        cout << "\n===== Error Report =====\n";
        cout << "Total errors: " << errorsource.size() << "\n\n";
        for (auto ln : errorsource) {
            cout << "Line: " << setw(4) << left << ln.lineNumber
                 << (ln.label.empty() ? " " : ln.label) << " "
                 << (ln.opcode.empty() ? " " : ln.opcode) << " "
                 << (ln.operand.empty() ? " " : ln.operand) << "\t"
                 << "Error: " << ln.errorMessage << "\n";
        
        }
    }else {
        flushT(tStart, tLen, tObject);

        for (const auto& ln : program) {
            if (ln.opcode == "END") {
                haveEND = true;
                auto it = symtab.find(ln.operand);
                if (it != symtab.end()) Erecord(it->second);
            }
        }
        ofstream outFile("object_program.txt");
        if (outFile.is_open()) {
            for (const auto& r : objectprogram)    outFile << r << "\n";
            for (const auto& r : modificationRecords) outFile << r << "\n";
            outFile << eRecord;
            outFile.close();
            cout << "Object program written to object_program.txt\n";
        } else {
            cerr << "Error: Cannot open object_program.txt for writing\n";
        }

        // 印出 Object Program
        for (const auto& r : objectprogram) cout << r << "\n";
        for (const auto& r : modificationRecords) cout << r << "\n";
        cout << eRecord ;
    }

}


//  MAIN
int main() {
    cleantxt("SICXE.txt");
    getopcode();
    parseLine();
    //printProgram();
    //printsymtab();
    outputtxt();   
    
    
    if(!_error)
        cout << "\n===== Pass 2 Object Program =====\n";
    runpass2();

    return 0;
}