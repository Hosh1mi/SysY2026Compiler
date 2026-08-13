#!/usr/bin/env python3
"""Generate AArch64 metadata and selection tables from the local DSL."""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field
from pathlib import Path
import re
import sys
from typing import Iterable


@dataclass(frozen=True)
class Location:
    path: Path
    line: int
    column: int

    def format(self) -> str:
        return f"{self.path}:{self.line}:{self.column}"


class DescriptionError(Exception):
    def __init__(self, location: Location, message: str):
        super().__init__(f"{location.format()}: error: {message}")


@dataclass(frozen=True)
class Token:
    kind: str
    text: str
    location: Location


class Lexer:
    """Small lexer with source locations; the DSL intentionally stays narrow."""

    SYMBOLS = set("{}[]=;,")

    def __init__(self, path: Path, source: str):
        self.path = path
        self.source = source
        self.offset = 0
        self.line = 1
        self.column = 1

    def location(self) -> Location:
        return Location(self.path, self.line, self.column)

    def advance(self) -> str:
        character = self.source[self.offset]
        self.offset += 1
        if character == "\n":
            self.line += 1
            self.column = 1
        else:
            self.column += 1
        return character

    def skip_trivia(self) -> None:
        while self.offset < len(self.source):
            if self.source[self.offset].isspace():
                self.advance()
                continue
            if self.source.startswith("//", self.offset):
                while (self.offset < len(self.source) and
                       self.source[self.offset] != "\n"):
                    self.advance()
                continue
            break

    def string(self, start: Location) -> Token:
        self.advance()
        value: list[str] = []
        escapes = {"n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\"}
        while self.offset < len(self.source):
            character = self.advance()
            if character == '"':
                return Token("string", "".join(value), start)
            if character == "\n":
                raise DescriptionError(start, "unterminated string literal")
            if character != "\\":
                value.append(character)
                continue
            if self.offset == len(self.source):
                break
            escaped = self.advance()
            if escaped not in escapes:
                raise DescriptionError(
                    start, f"unsupported escape sequence \\{escaped}")
            value.append(escapes[escaped])
        raise DescriptionError(start, "unterminated string literal")

    def tokens(self) -> list[Token]:
        result: list[Token] = []
        while True:
            self.skip_trivia()
            if self.offset == len(self.source):
                result.append(Token("eof", "", self.location()))
                return result

            start = self.location()
            character = self.source[self.offset]
            if character == '"':
                result.append(self.string(start))
                continue
            if character in self.SYMBOLS:
                result.append(Token("symbol", self.advance(), start))
                continue
            if character.isdigit():
                begin = self.offset
                while (self.offset < len(self.source) and
                       self.source[self.offset].isdigit()):
                    self.advance()
                result.append(Token("integer", self.source[begin:self.offset], start))
                continue
            if character.isalpha() or character == "_":
                begin = self.offset
                while (self.offset < len(self.source) and
                       (self.source[self.offset].isalnum() or
                        self.source[self.offset] == "_")):
                    self.advance()
                result.append(Token("identifier", self.source[begin:self.offset], start))
                continue
            raise DescriptionError(start, f"unexpected character {character!r}")


@dataclass(frozen=True)
class ParsedValue:
    kind: str
    value: str | int | tuple[str, ...]
    location: Location


@dataclass
class ParsedRecord:
    kind: str
    name: str
    location: Location
    fields: dict[str, ParsedValue] = field(default_factory=dict)


class Parser:
    def __init__(self, tokens: list[Token]):
        self.tokens = tokens
        self.index = 0

    def current(self) -> Token:
        return self.tokens[self.index]

    def consume(self) -> Token:
        token = self.current()
        self.index += 1
        return token

    def expect(self, kind: str, text: str | None = None) -> Token:
        token = self.current()
        if token.kind != kind or (text is not None and token.text != text):
            expected = repr(text) if text is not None else kind
            raise DescriptionError(token.location, f"expected {expected}")
        return self.consume()

    def parse_value(self) -> ParsedValue:
        token = self.current()
        if token.kind == "string":
            self.consume()
            return ParsedValue("string", token.text, token.location)
        if token.kind == "integer":
            self.consume()
            return ParsedValue("integer", int(token.text), token.location)
        if token.kind == "identifier":
            self.consume()
            return ParsedValue("identifier", token.text, token.location)
        if token.kind == "symbol" and token.text == "[":
            start = self.consume().location
            values: list[str] = []
            if self.current().text != "]":
                while True:
                    values.append(self.expect("identifier").text)
                    if self.current().text != ",":
                        break
                    self.consume()
            self.expect("symbol", "]")
            return ParsedValue("list", tuple(values), start)
        raise DescriptionError(token.location, "expected a field value")

    def parse(self) -> list[ParsedRecord]:
        records: list[ParsedRecord] = []
        while self.current().kind != "eof":
            keyword = self.expect("identifier")
            if keyword.text not in {
                "instruction", "dag", "pattern", "condition"
            }:
                raise DescriptionError(
                    keyword.location,
                    "expected 'instruction', 'dag', 'pattern', or "
                    "'condition'")
            name = self.expect("identifier")
            record = ParsedRecord(keyword.text, name.text, name.location)
            self.expect("symbol", "{")
            while self.current().text != "}":
                field_name = self.expect("identifier")
                if field_name.text in record.fields:
                    raise DescriptionError(
                        field_name.location,
                        f"duplicate field '{field_name.text}'")
                self.expect("symbol", "=")
                record.fields[field_name.text] = self.parse_value()
                self.expect("symbol", ";")
            self.consume()
            records.append(record)
        return records


# Order matches the boolean fields in InstrDesc's aggregate layout.
DESCRIPTOR_PROPERTIES = (
    "terminator", "branch", "call", "return", "load", "store",
    "side_effects", "pseudo", "sets_flags", "uses_flags",
)
EXTRA_PROPERTIES = {"commutable"}
RESOURCES = {
    "None", "ALU", "MAC", "Divide", "LoadStore", "Branch", "FPALU",
    "FPMulDiv",
}
IMMEDIATE_CONSTRAINTS = {
    "None", "AddSub12", "Shift32", "Shift64", "Logical32",
}
FIELDS = {
    "mnemonic", "defs", "operands", "latency", "resource", "properties",
    "immediate", "remat",
}
DAG_FIELDS = {"arity", "mode", "address"}
PATTERN_FIELDS = {
    "root", "result", "source", "emit", "operands", "predicate",
    "priority", "source_index", "memory",
}
DAG_MODES = {"ignore", "generated", "custom", "hybrid"}
VALUE_TYPES = {
    "Invalid", "I1", "I32", "I64", "F32", "Ptr", "V4I32", "V4F32",
    "Flags",
}
PATTERN_TYPES = VALUE_TYPES | {"Any"}
PATTERN_PREDICATES = {
    "None", "target_immediate", "lane", "operand_count_1",
    "direct_global", "not_direct_global",
}
MEMORY_ACTIONS = {"None", "load", "store"}
CONDITION_FIELDS = {"family", "code"}
CONDITION_FAMILIES = {"integer", "floating"}
CONDITION_CODES = {
    "EQ", "NE", "HS", "LO", "MI", "PL", "VS", "VC", "HI", "LS",
    "GE", "LT", "GT", "LE", "AL",
}
EXPECTED_CONDITIONS = {
    "integer": {
        "ICMP_EQ", "ICMP_NE", "ICMP_UGT", "ICMP_UGE", "ICMP_ULT",
        "ICMP_ULE", "ICMP_SGT", "ICMP_SGE", "ICMP_SLT", "ICMP_SLE",
    },
    "floating": {
        "FCMP_FALSE", "FCMP_OEQ", "FCMP_OGT", "FCMP_OGE", "FCMP_OLT",
        "FCMP_OLE", "FCMP_ONE", "FCMP_ORD", "FCMP_UNO", "FCMP_UEQ",
        "FCMP_UGT", "FCMP_UGE", "FCMP_ULT", "FCMP_ULE", "FCMP_UNE",
        "FCMP_TRUE",
    },
}
MAX_UINT8 = (1 << 8) - 1
MAX_INT8 = (1 << 7) - 1
MAX_UINT16 = (1 << 16) - 1
MAX_UINT32 = (1 << 32) - 1
MAX_INT32 = (1 << 31) - 1


@dataclass(frozen=True)
class Instruction:
    name: str
    mnemonic: str
    defs: int
    operands: int
    latency: int
    resource: str
    properties: frozenset[str]
    immediate: str
    remat: int
    location: Location


@dataclass(frozen=True)
class DAGOpcode:
    name: str
    arity: int | None
    mode: str
    address: int | None
    location: Location


@dataclass(frozen=True)
class PatternOperand:
    kind: str
    index: int
    tied_to: int = -1
    early_clobber: bool = False


@dataclass(frozen=True)
class Pattern:
    name: str
    root: str
    result: str
    source: str | None
    source_index: int
    emit: str
    operands: tuple[PatternOperand, ...]
    predicate: str
    memory: str
    priority: int
    location: Location


@dataclass(frozen=True)
class Condition:
    name: str
    family: str
    code: str
    location: Location


def typed_field(record: ParsedRecord, name: str, kind: str,
                default: object) -> object:
    value = record.fields.get(name)
    if value is None:
        return default
    if value.kind != kind:
        raise DescriptionError(value.location, f"field '{name}' requires {kind}")
    return value.value


def validate_instructions(records: Iterable[ParsedRecord]) -> list[Instruction]:
    result: list[Instruction] = []
    names: dict[str, Location] = {}
    for record in records:
        if record.name in names:
            raise DescriptionError(record.location, f"duplicate instruction '{record.name}'")
        names[record.name] = record.location

        unknown_fields = set(record.fields) - FIELDS
        if unknown_fields:
            name = sorted(unknown_fields)[0]
            raise DescriptionError(record.fields[name].location, f"unknown field '{name}'")

        mnemonic = typed_field(record, "mnemonic", "string", None)
        if mnemonic is None:
            raise DescriptionError(record.location, "missing required field 'mnemonic'")
        defs = typed_field(record, "defs", "integer", 0)
        operands = typed_field(record, "operands", "integer", 0)
        latency = typed_field(record, "latency", "integer", 1)
        resource = typed_field(record, "resource", "identifier", "ALU")
        property_list = typed_field(record, "properties", "list", ())
        immediate = typed_field(record, "immediate", "identifier", "None")
        remat = typed_field(record, "remat", "integer", 0)

        assert isinstance(defs, int) and isinstance(operands, int)
        assert isinstance(latency, int) and isinstance(remat, int)
        assert isinstance(resource, str) and isinstance(immediate, str)
        assert isinstance(mnemonic, str) and isinstance(property_list, tuple)
        properties = frozenset(property_list)
        if record.name == "Invalid" or record.name == "Count":
            raise DescriptionError(record.location, f"reserved opcode name '{record.name}'")
        for field_name, value in (("defs", defs), ("operands", operands),
                                  ("latency", latency), ("remat", remat)):
            if value > MAX_UINT32:
                field = record.fields.get(field_name)
                location = field.location if field is not None else record.location
                raise DescriptionError(
                    location, f"field '{field_name}' exceeds unsigned range")
        if resource not in RESOURCES:
            raise DescriptionError(record.fields["resource"].location,
                                   f"unknown scheduling resource '{resource}'")
        if len(properties) != len(property_list):
            raise DescriptionError(record.fields["properties"].location,
                                   "duplicate instruction property")
        unknown_properties = properties - set(DESCRIPTOR_PROPERTIES) - EXTRA_PROPERTIES
        if unknown_properties:
            name = sorted(unknown_properties)[0]
            raise DescriptionError(record.fields["properties"].location,
                                   f"unknown instruction property '{name}'")
        if immediate not in IMMEDIATE_CONSTRAINTS:
            raise DescriptionError(record.fields["immediate"].location,
                                   f"unknown immediate constraint '{immediate}'")
        if "branch" in properties and "terminator" not in properties:
            raise DescriptionError(record.location, "branch instructions must be terminators")
        if "return" in properties and "terminator" not in properties:
            raise DescriptionError(record.location, "return instructions must be terminators")
        result.append(Instruction(record.name, mnemonic, defs, operands,
                                  latency, resource, properties, immediate,
                                  remat, record.location))
    if not result:
        raise DescriptionError(Location(Path("<input>"), 1, 1),
                               "target description contains no instructions")
    # Invalid and Count consume two values in the generated uint16_t enum.
    if len(result) > MAX_UINT16 - 1:
        raise DescriptionError(result[-1].location,
                               "too many target instructions for Opcode")
    return result


def validate_dag_opcodes(records: Iterable[ParsedRecord]) -> list[DAGOpcode]:
    result: list[DAGOpcode] = []
    names: set[str] = set()
    for record in records:
        if record.name in names:
            raise DescriptionError(
                record.location, f"duplicate DAG opcode '{record.name}'")
        names.add(record.name)
        unknown_fields = set(record.fields) - DAG_FIELDS
        if unknown_fields:
            name = sorted(unknown_fields)[0]
            raise DescriptionError(record.fields[name].location,
                                   f"unknown DAG field '{name}'")

        arity_value = record.fields.get("arity")
        if arity_value is None:
            raise DescriptionError(record.location,
                                   "missing required field 'arity'")
        if arity_value.kind == "integer":
            assert isinstance(arity_value.value, int)
            arity: int | None = arity_value.value
        elif (arity_value.kind == "identifier" and
              arity_value.value == "variadic"):
            arity = None
        else:
            raise DescriptionError(
                arity_value.location,
                "field 'arity' requires an integer or 'variadic'")

        mode = typed_field(record, "mode", "identifier", None)
        if mode is None:
            raise DescriptionError(record.location,
                                   "missing required field 'mode'")
        assert isinstance(mode, str)
        if mode not in DAG_MODES:
            raise DescriptionError(record.fields["mode"].location,
                                   f"unknown selection mode '{mode}'")
        if record.name == "Count":
            raise DescriptionError(record.location,
                                   "reserved DAG opcode name 'Count'")
        address = typed_field(record, "address", "integer", None)
        assert address is None or isinstance(address, int)
        if address is not None and address > MAX_INT32:
            raise DescriptionError(record.fields["address"].location,
                                   "address operand exceeds generated range")
        if address is not None and arity is not None and address >= arity:
            raise DescriptionError(record.fields["address"].location,
                                   "address operand is outside DAG arity")
        if address is not None and mode == "ignore":
            raise DescriptionError(record.fields["address"].location,
                                   "ignored DAG opcodes cannot have addresses")
        result.append(
            DAGOpcode(record.name, arity, mode, address, record.location))

    if not result:
        raise DescriptionError(Location(Path("<input>"), 1, 1),
                               "target description contains no DAG opcodes")
    if result[0].name != "Invalid":
        raise DescriptionError(result[0].location,
                               "the first DAG opcode must be 'Invalid'")
    by_name = {opcode.name: opcode for opcode in result}
    for required in ("Invalid", "EntryToken"):
        opcode = by_name.get(required)
        if opcode is None:
            raise DescriptionError(result[0].location,
                                   f"missing required DAG opcode '{required}'")
        if opcode.mode != "ignore":
            raise DescriptionError(opcode.location,
                                   f"DAG opcode '{required}' must be ignored")
    if len(result) > MAX_UINT16:
        raise DescriptionError(result[-1].location,
                               "too many DAG opcodes for SDOpcode")
    custom_count = sum(opcode.mode in {"custom", "hybrid"}
                       for opcode in result)
    # None and Count consume two values in the generated uint8_t enum.
    if custom_count > MAX_UINT8 - 1:
        raise DescriptionError(result[-1].location,
                               "too many custom DAG selectors")
    return result


def parse_pattern_operand(text: str, location: Location) -> PatternOperand:
    fixed = {
        "def": ("Definition", 0),
        "node_integer": ("NodeInteger", 0),
        "frame_index": ("FrameIndex", 0),
        "node_global": ("NodeGlobal", 0),
        "zero": ("Zero", 0),
    }
    match = re.fullmatch(r"(.+?)(?:_(early|tied[0-9]+))?", text)
    assert match is not None
    base, modifier = match.groups()
    tied_to = -1
    early_clobber = modifier == "early"
    if modifier and modifier.startswith("tied"):
        tied_to = int(modifier[4:])
    if base in fixed:
        kind, index = fixed[base]
        return PatternOperand(kind, index, tied_to, early_clobber)
    match = re.fullmatch(r"(use|imm|block|global)([0-9]+)", base)
    if not match:
        raise DescriptionError(location,
                               f"unknown pattern operand '{text}'")
    kind = {
        "use": "Use",
        "imm": "OperandInteger",
        "block": "Block",
        "global": "OperandGlobal",
    }[match.group(1)]
    return PatternOperand(kind, int(match.group(2)), tied_to, early_clobber)


def validate_patterns(
    records: Iterable[ParsedRecord], instructions: list[Instruction],
    dag_opcodes: list[DAGOpcode]
) -> list[Pattern]:
    instruction_by_name = {instruction.name: instruction
                           for instruction in instructions}
    dag_by_name = {opcode.name: opcode for opcode in dag_opcodes}
    result: list[Pattern] = []
    names: set[str] = set()
    for record in records:
        if record.name in names:
            raise DescriptionError(record.location,
                                   f"duplicate pattern '{record.name}'")
        names.add(record.name)
        unknown_fields = set(record.fields) - PATTERN_FIELDS
        if unknown_fields:
            name = sorted(unknown_fields)[0]
            raise DescriptionError(record.fields[name].location,
                                   f"unknown pattern field '{name}'")

        required = ("root", "result", "emit", "operands")
        for field_name in required:
            if field_name not in record.fields:
                raise DescriptionError(
                    record.location,
                    f"missing required field '{field_name}'")
        root = typed_field(record, "root", "identifier", None)
        result_type = typed_field(record, "result", "identifier", None)
        source = typed_field(record, "source", "identifier", None)
        source_index = typed_field(record, "source_index", "integer", 0)
        emit = typed_field(record, "emit", "identifier", None)
        operand_names = typed_field(record, "operands", "list", None)
        predicate = typed_field(record, "predicate", "identifier", "None")
        memory = typed_field(record, "memory", "identifier", "None")
        priority = typed_field(record, "priority", "integer", 0)
        assert isinstance(root, str) and isinstance(result_type, str)
        assert source is None or isinstance(source, str)
        assert isinstance(source_index, int)
        assert isinstance(emit, str) and isinstance(operand_names, tuple)
        assert isinstance(predicate, str) and isinstance(memory, str)
        assert isinstance(priority, int)

        if root not in dag_by_name:
            raise DescriptionError(record.fields["root"].location,
                                   f"unknown DAG opcode '{root}'")
        if result_type not in PATTERN_TYPES:
            raise DescriptionError(record.fields["result"].location,
                                   f"unknown result type '{result_type}'")
        if source is not None and source not in PATTERN_TYPES:
            raise DescriptionError(record.fields["source"].location,
                                   f"unknown source type '{source}'")
        if source is None and "source_index" in record.fields:
            raise DescriptionError(record.fields["source_index"].location,
                                   "source_index requires a source type")
        if (source is not None and dag_by_name[root].arity is not None and
            source_index >= dag_by_name[root].arity):
            raise DescriptionError(record.fields["source"].location,
                                   "source type operand is outside DAG arity")
        if emit not in instruction_by_name:
            raise DescriptionError(record.fields["emit"].location,
                                   f"unknown target opcode '{emit}'")
        if predicate not in PATTERN_PREDICATES:
            raise DescriptionError(record.fields["predicate"].location,
                                   f"unknown pattern predicate '{predicate}'")
        if memory not in MEMORY_ACTIONS:
            raise DescriptionError(record.fields["memory"].location,
                                   f"unknown memory action '{memory}'")
        expected_memory_root = {"load": "Load", "store": "Store"}
        if (memory != "None" and
            root != expected_memory_root[memory]):
            raise DescriptionError(
                record.fields["memory"].location,
                f"memory action '{memory}' requires root "
                f"'{expected_memory_root[memory]}'")
        if (predicate in {"direct_global", "not_direct_global"} and
            root != "GlobalAddress" and dag_by_name[root].address is None):
            raise DescriptionError(
                record.fields["predicate"].location,
                "direct-global predicate requires a DAG address operand")

        operands = tuple(parse_pattern_operand(name,
                         record.fields["operands"].location)
                         for name in operand_names)
        if len(operands) > MAX_UINT8:
            raise DescriptionError(record.fields["operands"].location,
                                   "pattern has too many emitted operands")
        for operand in operands:
            if operand.index > MAX_UINT8:
                raise DescriptionError(
                    record.fields["operands"].location,
                    "pattern operand index exceeds generated range")
            if operand.tied_to > MAX_INT8:
                raise DescriptionError(
                    record.fields["operands"].location,
                    "tied operand index exceeds generated range")
        instruction = instruction_by_name[emit]
        if len(operands) != instruction.operands:
            raise DescriptionError(
                record.fields["operands"].location,
                f"pattern emits {len(operands)} operands but '{emit}' "
                f"requires {instruction.operands}")
        definition_count = sum(operand.kind == "Definition"
                               for operand in operands)
        if definition_count != instruction.defs:
            raise DescriptionError(
                record.fields["operands"].location,
                f"pattern emits {definition_count} definitions but '{emit}' "
                f"requires {instruction.defs}")
        for index, operand in enumerate(operands):
            if operand.early_clobber and operand.kind != "Definition":
                raise DescriptionError(
                    record.fields["operands"].location,
                    "early-clobber is only valid on definitions")
            if operand.tied_to >= 0:
                if operand.kind == "Definition":
                    raise DescriptionError(
                        record.fields["operands"].location,
                        "a definition cannot be tied to another operand")
                if operand.tied_to >= len(operands):
                    raise DescriptionError(
                        record.fields["operands"].location,
                        f"operand {index} has an out-of-range tie")
                if operands[operand.tied_to].kind != "Definition":
                    raise DescriptionError(
                        record.fields["operands"].location,
                        f"operand {index} must tie to a definition")

        dag_opcode = dag_by_name[root]
        for operand in operands:
            if operand.kind in {"Use", "OperandInteger"}:
                if dag_opcode.arity is not None and operand.index >= dag_opcode.arity:
                    raise DescriptionError(
                        record.fields["operands"].location,
                        f"operand {operand.index} is outside '{root}' arity")
            elif operand.kind == "Block" and root not in {"Branch", "BranchCond"}:
                raise DescriptionError(
                    record.fields["operands"].location,
                    "block operands are only valid on branches")
            elif operand.kind == "NodeInteger" and root != "Constant":
                raise DescriptionError(
                    record.fields["operands"].location,
                    "node_integer is only valid on constants")
            elif operand.kind == "FrameIndex" and root != "FrameIndex":
                raise DescriptionError(
                    record.fields["operands"].location,
                    "frame_index is only valid on frame-index nodes")
            elif operand.kind == "NodeGlobal" and root != "GlobalAddress":
                raise DescriptionError(
                    record.fields["operands"].location,
                    "node_global is only valid on global-address nodes")
            elif operand.kind == "OperandGlobal":
                if predicate != "direct_global":
                    raise DescriptionError(
                        record.fields["operands"].location,
                        "global operands require the direct_global predicate")
                if dag_opcode.address != operand.index:
                    raise DescriptionError(
                        record.fields["operands"].location,
                        "global operand must use the DAG address operand")

        immediate_operands = [operand for operand in operands
                              if operand.kind == "OperandInteger"]
        if predicate in {"target_immediate", "lane"} and len(immediate_operands) != 1:
            raise DescriptionError(
                record.fields["predicate"].location,
                f"predicate '{predicate}' requires one immediate operand")
        if (predicate == "target_immediate" and
            instruction.immediate == "None"):
            raise DescriptionError(
                record.fields["emit"].location,
                f"'{emit}' has no immediate constraint")
        if memory != "None" and memory not in instruction.properties:
            raise DescriptionError(
                record.fields["memory"].location,
                f"'{emit}' is not marked as a {memory} instruction")

        def type_overlap(lhs: str | None, rhs: str | None) -> bool:
            return lhs in {None, "Any"} or rhs in {None, "Any"} or lhs == rhs

        for previous in result:
            if previous.root != root or previous.priority != priority:
                continue
            if not type_overlap(previous.result, result_type):
                continue
            source_overlap = True
            if (previous.source is not None and source is not None and
                previous.source_index == source_index):
                source_overlap = type_overlap(previous.source, source)
            mutually_exclusive = {
                previous.predicate, predicate
            } == {"direct_global", "not_direct_global"}
            if source_overlap and not mutually_exclusive:
                raise DescriptionError(
                    record.location,
                    f"pattern overlaps '{previous.name}' at equal priority")
        result.append(Pattern(record.name, root, result_type, source,
                              source_index, emit, operands, predicate,
                              memory, priority,
                              record.location))

    roots_with_patterns = {pattern.root for pattern in result}
    for opcode in dag_opcodes:
        if (opcode.mode in {"generated", "hybrid"} and
            opcode.name not in roots_with_patterns):
            raise DescriptionError(
                opcode.location,
                f"DAG opcode '{opcode.name}' requires at least one pattern")
        if (opcode.mode == "ignore" and opcode.name in roots_with_patterns):
            raise DescriptionError(
                opcode.location,
                f"ignored DAG opcode '{opcode.name}' cannot have patterns")
        if (opcode.mode == "custom" and opcode.name in roots_with_patterns):
            raise DescriptionError(
                opcode.location,
                f"custom DAG opcode '{opcode.name}' must use hybrid mode "
                "when it has patterns")
    return result


def validate_conditions(records: Iterable[ParsedRecord]) -> list[Condition]:
    result: list[Condition] = []
    names: set[str] = set()
    for record in records:
        if record.name in names:
            raise DescriptionError(record.location,
                                   f"duplicate condition '{record.name}'")
        names.add(record.name)
        unknown_fields = set(record.fields) - CONDITION_FIELDS
        if unknown_fields:
            name = sorted(unknown_fields)[0]
            raise DescriptionError(record.fields[name].location,
                                   f"unknown condition field '{name}'")
        family = typed_field(record, "family", "identifier", None)
        code = typed_field(record, "code", "identifier", None)
        if family is None or code is None:
            missing = "family" if family is None else "code"
            raise DescriptionError(
                record.location, f"missing required field '{missing}'")
        assert isinstance(family, str) and isinstance(code, str)
        if family not in CONDITION_FAMILIES:
            raise DescriptionError(record.fields["family"].location,
                                   f"unknown condition family '{family}'")
        if code not in CONDITION_CODES:
            raise DescriptionError(record.fields["code"].location,
                                   f"unknown condition code '{code}'")
        expected_prefix = "ICMP_" if family == "integer" else "FCMP_"
        if not record.name.startswith(expected_prefix):
            raise DescriptionError(
                record.location,
                f"{family} condition must start with '{expected_prefix}'")
        result.append(Condition(record.name, family, code, record.location))
    if not result:
        raise DescriptionError(Location(Path("<input>"), 1, 1),
                               "target description contains no conditions")
    for family, expected in EXPECTED_CONDITIONS.items():
        actual = {condition.name for condition in result
                  if condition.family == family}
        if actual != expected:
            missing = sorted(expected - actual)
            extra = sorted(actual - expected)
            detail = (f"missing {', '.join(missing)}" if missing else
                      f"unexpected {', '.join(extra)}")
            raise DescriptionError(result[0].location,
                                   f"incomplete {family} conditions: {detail}")
    return result


def cpp_string(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    escaped = (escaped.replace("\n", "\\n").replace("\t", "\\t")
               .replace("\r", "\\r"))
    return f'"{escaped}"'


def generated_banner(source: Path, purpose: str) -> str:
    return (f"// {purpose}\n"
            "// Generated from " + source.name + " by generate.py.\n"
            "// Edit the target description or generator, not this file.\n")


def emit_opcodes(source: Path, instructions: list[Instruction]) -> str:
    entries = "\n".join(
        f"    {instruction.name}," for instruction in instructions)
    return f"""{generated_banner(source, "This file defines AArch64 machine opcodes.")}#pragma once

#include <cstdint>

namespace backend::aarch64 {{

enum class Opcode : std::uint16_t {{
    Invalid,
{entries}
    Count,
}};

}} // namespace backend::aarch64
"""


def emit_dag_opcodes(source: Path, dag_opcodes: list[DAGOpcode]) -> str:
    entries = "\n".join(f"    {opcode.name}," for opcode in dag_opcodes)
    names = "\n".join(
        f"    case SDOpcode::{opcode.name}: return \"{opcode.name}\";"
        for opcode in dag_opcodes)
    return f"""{generated_banner(source, "This file defines SelectionDAG opcodes and their names.")}#pragma once

#include <cstdint>

namespace backend::aarch64 {{

enum class SDOpcode : std::uint16_t {{
{entries}
    Count,
}};

constexpr const char *sdOpcodeName(SDOpcode opcode) {{
    switch (opcode) {{
{names}
    case SDOpcode::Count: break;
    }}
    return "Invalid";
}}

}} // namespace backend::aarch64
"""


def emit_info_header(source: Path) -> str:
    constraints = "\n".join(
        f"    {name}," for name in sorted(IMMEDIATE_CONSTRAINTS))
    return f"""{generated_banner(source, "This file declares generated AArch64 instruction queries.")}#pragma once

#include "backend/target.hpp"

#include <cstdint>

namespace backend::aarch64::generated {{

enum class ImmediateConstraint : std::uint8_t {{
{constraints}
}};

const InstrDesc &instructionDescriptor(Opcode opcode);
ImmediateConstraint immediateConstraint(Opcode opcode);
bool isCommutable(Opcode opcode);
unsigned rematerializationCost(Opcode opcode);

}} // namespace backend::aarch64::generated
"""


def emit_isel_header(
    source: Path, patterns: list[Pattern], dag_opcodes: list[DAGOpcode]
) -> str:
    max_operands = max(len(pattern.operands) for pattern in patterns)
    custom_entries = "\n".join(
        f"    {opcode.name}," for opcode in dag_opcodes
        if opcode.mode in {"custom", "hybrid"})
    return f"""{generated_banner(source, "This file declares generated AArch64 selection tables.")}#pragma once

#include "backend/selection_dag.hpp"

#include <array>
#include <cstdint>

namespace backend::aarch64::generated {{

enum class SelectionMode : std::uint8_t {{ Ignore, Generated, Custom, Hybrid }};
enum class CustomSelector : std::uint8_t {{
    None,
{custom_entries}
    Count,
}};
enum class PatternOperandKind : std::uint8_t {{
    Definition,
    Use,
    OperandInteger,
    NodeInteger,
    FrameIndex,
    NodeGlobal,
    Block,
    OperandGlobal,
    Zero,
}};
enum class PatternMemoryAction : std::uint8_t {{ None, Load, Store }};

struct PatternOperand {{
    PatternOperandKind kind;
    std::uint8_t index;
    std::int8_t tiedTo;
    bool earlyClobber;
}};

struct SelectionPattern {{
    const char *name;
    Opcode opcode;
    std::array<PatternOperand, {max_operands}> operands;
    std::uint8_t operandCount;
    PatternMemoryAction memory;
}};

SelectionMode selectionMode(SDOpcode opcode);
CustomSelector customSelector(SDOpcode opcode);
int dagAddressOperand(SDOpcode opcode);
const SelectionPattern *matchPattern(const SDNode &node, bool directGlobal);
CondCode integerCondition(int predicate);
CondCode floatingCondition(int predicate);

}} // namespace backend::aarch64::generated
"""


def bool_literal(value: bool) -> str:
    return "true" if value else "false"


def emit_info_source(source: Path, instructions: list[Instruction]) -> str:
    descriptors: list[str] = [
        "    InstrDesc{}, // Invalid"
    ]
    constraints: list[str] = ["    ImmediateConstraint::None, // Invalid"]
    commutable: list[str] = ["    false, // Invalid"]
    remat: list[str] = ["    0, // Invalid"]
    for instruction in instructions:
        flags = [bool_literal(name in instruction.properties)
                 for name in DESCRIPTOR_PROPERTIES]
        descriptors.append(
            "    InstrDesc{Opcode::%s, %s, %u, %u, %s, %s, %s, %s, "
            "%s, %s, %s, %s, %s, %s, %u, SchedResource::%s}," %
            (instruction.name, cpp_string(instruction.mnemonic),
             instruction.defs, instruction.operands, *flags,
             instruction.latency, instruction.resource))
        constraints.append(
            f"    ImmediateConstraint::{instruction.immediate}, // {instruction.name}")
        commutable.append(
            f"    {bool_literal('commutable' in instruction.properties)}, // {instruction.name}")
        remat.append(f"    {instruction.remat}, // {instruction.name}")

    count = len(instructions) + 1
    return f"""{generated_banner(source, "This file defines generated AArch64 instruction metadata.")}#include "backend/aarch64_instruction_info.hpp"

#include <array>
#include <cstddef>

namespace backend::aarch64::generated {{
namespace {{

constexpr std::size_t kOpcodeCount = static_cast<std::size_t>(Opcode::Count);

constexpr std::array<InstrDesc, kOpcodeCount> kDescriptors{{{{
{chr(10).join(descriptors)}
}}}};

constexpr std::array<ImmediateConstraint, kOpcodeCount> kImmediateConstraints{{{{
{chr(10).join(constraints)}
}}}};

constexpr std::array<bool, kOpcodeCount> kCommutable{{{{
{chr(10).join(commutable)}
}}}};

constexpr std::array<unsigned, kOpcodeCount> kRematerializationCosts{{{{
{chr(10).join(remat)}
}}}};

std::size_t opcodeIndex(Opcode opcode) {{
    const auto index = static_cast<std::size_t>(opcode);
    return index < kOpcodeCount ? index : 0;
}}

static_assert(kOpcodeCount == {count}, "generated opcode table is incomplete");

}} // namespace

const InstrDesc &instructionDescriptor(Opcode opcode) {{
    return kDescriptors[opcodeIndex(opcode)];
}}

ImmediateConstraint immediateConstraint(Opcode opcode) {{
    return kImmediateConstraints[opcodeIndex(opcode)];
}}

bool isCommutable(Opcode opcode) {{
    return kCommutable[opcodeIndex(opcode)];
}}

unsigned rematerializationCost(Opcode opcode) {{
    return kRematerializationCosts[opcodeIndex(opcode)];
}}

}} // namespace backend::aarch64::generated
"""


def emit_isel_source(
    source: Path, dag_opcodes: list[DAGOpcode], patterns: list[Pattern],
    conditions: list[Condition]
) -> str:
    mode_names = {
        "ignore": "Ignore",
        "generated": "Generated",
        "custom": "Custom",
        "hybrid": "Hybrid",
    }
    modes = "\n".join(
        f"    SelectionMode::{mode_names[opcode.mode]}, // {opcode.name}"
        for opcode in dag_opcodes)
    custom_values = "\n".join(
        f"    CustomSelector::{opcode.name}, // {opcode.name}"
        if opcode.mode in {"custom", "hybrid"}
        else f"    CustomSelector::None, // {opcode.name}"
        for opcode in dag_opcodes)
    addresses = "\n".join(
        f"    {opcode.address if opcode.address is not None else -1}, // "
        f"{opcode.name}"
        for opcode in dag_opcodes)

    ordered_patterns = sorted(
        patterns,
        key=lambda pattern: (
            next(index for index, opcode in enumerate(dag_opcodes)
                 if opcode.name == pattern.root),
            -pattern.priority,
            pattern.name,
        ))
    entries: list[str] = []
    checks_by_root: dict[str, list[str]] = {}
    max_operands = max(len(pattern.operands) for pattern in ordered_patterns)
    for index, pattern in enumerate(ordered_patterns):
        padded_operands = list(pattern.operands)
        while len(padded_operands) < max_operands:
            padded_operands.append(PatternOperand("Definition", 0))
        operands = ", ".join(
            "PatternOperand{PatternOperandKind::%s, %d, %d, %s}" %
            (operand.kind, operand.index, operand.tied_to,
             bool_literal(operand.early_clobber))
            for operand in padded_operands)
        entries.append(
            "    SelectionPattern{%s, Opcode::%s, {%s}, %d, "
            "PatternMemoryAction::%s}," %
            (cpp_string(pattern.name), pattern.emit, operands,
             len(pattern.operands),
             pattern.memory[0].upper() + pattern.memory[1:]))

        result_check = ("true" if pattern.result == "Any" else
                        f"resultType == ValueType::{pattern.result}")
        source_check = "true"
        if pattern.source is not None and pattern.source != "Any":
            source_check = (
                f"operandType(node, {pattern.source_index}) == "
                f"ValueType::{pattern.source}")
        predicate_check = "true"
        immediate_operands = [operand for operand in pattern.operands
                              if operand.kind == "OperandInteger"]
        if pattern.predicate == "target_immediate":
            operand = immediate_operands[0]
            predicate_check = (
                f"matchesTargetImmediate(node, {operand.index}, "
                f"Opcode::{pattern.emit})")
        elif pattern.predicate == "lane":
            operand = immediate_operands[0]
            predicate_check = f"matchesLane(node, {operand.index})"
        elif pattern.predicate == "operand_count_1":
            predicate_check = "node.operands().size() == 1"
        elif pattern.predicate == "direct_global":
            predicate_check = "directGlobal"
        elif pattern.predicate == "not_direct_global":
            predicate_check = "!directGlobal"
        checks_by_root.setdefault(pattern.root, []).append(
            f"        if ({result_check} && {source_check} &&\n"
            f"            {predicate_check})\n"
            f"            return &kPatterns[{index}];")

    match_cases: list[str] = []
    for opcode in dag_opcodes:
        root_checks = checks_by_root.get(opcode.name)
        if root_checks is None:
            continue
        match_cases.append(
            f"    case SDOpcode::{opcode.name}:\n" +
            "\n".join(root_checks) +
            "\n        break;")

    condition_cases: dict[str, list[str]] = {
        "integer": [], "floating": [],
    }
    family_classes = {"integer": "ICmpInst", "floating": "FCmpInst"}
    for condition in conditions:
        condition_cases[condition.family].append(
            f"    case {family_classes[condition.family]}::"
            f"{condition.name}: return CondCode::{condition.code};")

    return f"""{generated_banner(source, "This file defines generated AArch64 selection tables and matchers.")}#include "backend/aarch64_isel.hpp"

#include "mid/ir/instruction.hpp"

#include <array>
#include <cstddef>
#include <stdexcept>

namespace backend::aarch64::generated {{
namespace {{

constexpr std::size_t kDAGOpcodeCount =
    static_cast<std::size_t>(SDOpcode::Count);

constexpr std::array<SelectionMode, kDAGOpcodeCount> kSelectionModes{{{{
{modes}
}}}};

constexpr std::array<CustomSelector, kDAGOpcodeCount> kCustomSelectors{{{{
{custom_values}
}}}};

constexpr std::array<int, kDAGOpcodeCount> kAddressOperands{{{{
{addresses}
}}}};

constexpr std::array<SelectionPattern, {len(ordered_patterns)}> kPatterns{{{{
{chr(10).join(entries)}
}}}};

ValueType resultType(const SDNode &node) {{
    return node.resultTypes().empty() ? ValueType::Invalid
                                      : node.resultTypes().front();
}}

ValueType operandType(const SDNode &node, unsigned index) {{
    if (index >= node.operands().size() || !node.operands()[index].node)
        return ValueType::Invalid;
    const SDValue source = node.operands()[index];
    return source.result < source.node->resultTypes().size()
               ? source.node->resultTypes()[source.result]
               : ValueType::Invalid;
}}

const SDNode *constantOperand(const SDNode &node, unsigned index) {{
    if (index >= node.operands().size())
        return nullptr;
    const SDNode *operand = node.operands()[index].node;
    return operand && operand->opcode() == SDOpcode::Constant ? operand
                                                              : nullptr;
}}

bool matchesTargetImmediate(const SDNode &node, unsigned index,
                            Opcode opcode) {{
    const SDNode *constant = constantOperand(node, index);
    return constant && InstrInfo::acceptsImmediate(opcode, constant->integer);
}}

bool matchesLane(const SDNode &node, unsigned index) {{
    const SDNode *constant = constantOperand(node, index);
    return constant && constant->integer >= 0 && constant->integer < 4;
}}

}} // namespace

SelectionMode selectionMode(SDOpcode opcode) {{
    const auto index = static_cast<std::size_t>(opcode);
    return index < kDAGOpcodeCount ? kSelectionModes[index]
                                   : SelectionMode::Custom;
}}

CustomSelector customSelector(SDOpcode opcode) {{
    const auto index = static_cast<std::size_t>(opcode);
    return index < kDAGOpcodeCount ? kCustomSelectors[index]
                                   : CustomSelector::None;
}}

int dagAddressOperand(SDOpcode opcode) {{
    const auto index = static_cast<std::size_t>(opcode);
    return index < kDAGOpcodeCount ? kAddressOperands[index] : -1;
}}

const SelectionPattern *matchPattern(const SDNode &node,
                                     bool directGlobal) {{
    const ValueType resultType = generated::resultType(node);
    switch (node.opcode()) {{
{chr(10).join(match_cases)}
    case SDOpcode::Count:
        break;
    }}
    return nullptr;
}}

CondCode integerCondition(int predicate) {{
    switch (static_cast<ICmpInst::ICmpOp>(predicate)) {{
{chr(10).join(condition_cases['integer'])}
    }}
    throw std::logic_error("unknown integer predicate");
}}

CondCode floatingCondition(int predicate) {{
    switch (static_cast<FCmpInst::FCmpOp>(predicate)) {{
{chr(10).join(condition_cases['floating'])}
    }}
    throw std::logic_error("unknown floating predicate");
}}

}} // namespace backend::aarch64::generated
"""


def write_if_changed(path: Path, content: str) -> None:
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def main() -> int:
    argument_parser = argparse.ArgumentParser()
    argument_parser.add_argument("--input", required=True, type=Path)
    argument_parser.add_argument("--header-dir", required=True, type=Path)
    argument_parser.add_argument("--source-dir", required=True, type=Path)
    arguments = argument_parser.parse_args()

    try:
        source = arguments.input.read_text(encoding="utf-8")
        records = Parser(Lexer(arguments.input, source).tokens()).parse()
        instructions = validate_instructions(
            record for record in records if record.kind == "instruction")
        dag_opcodes = validate_dag_opcodes(
            record for record in records if record.kind == "dag")
        patterns = validate_patterns(
            (record for record in records if record.kind == "pattern"),
            instructions, dag_opcodes)
        conditions = validate_conditions(
            record for record in records if record.kind == "condition")
        write_if_changed(arguments.header_dir / "aarch64_opcodes.hpp",
                         emit_opcodes(arguments.input, instructions))
        write_if_changed(arguments.header_dir / "aarch64_dag_opcodes.hpp",
                         emit_dag_opcodes(arguments.input, dag_opcodes))
        write_if_changed(arguments.header_dir / "aarch64_instruction_info.hpp",
                         emit_info_header(arguments.input))
        write_if_changed(arguments.source_dir / "aarch64_instruction_info.cpp",
                         emit_info_source(arguments.input, instructions))
        write_if_changed(arguments.header_dir / "aarch64_isel.hpp",
                         emit_isel_header(arguments.input, patterns,
                                          dag_opcodes))
        write_if_changed(arguments.source_dir / "aarch64_isel.cpp",
                         emit_isel_source(arguments.input, dag_opcodes,
                                          patterns, conditions))
    except (OSError, DescriptionError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
