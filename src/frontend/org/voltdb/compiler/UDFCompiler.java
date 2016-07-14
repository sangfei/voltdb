/* This file is part of VoltDB.
 * Copyright (C) 2008-2016 VoltDB Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with VoltDB.  If not, see <http://www.gnu.org/licenses/>.
 */

package org.voltdb.compiler;

import java.util.regex.Matcher;

import org.voltdb.catalog.CatalogMap;
import org.voltdb.catalog.Database;
import org.voltdb.catalog.UDF;
import org.voltdb.catalog.UDFLibrary;
import org.voltdb.catalog.UDFArgument;
import org.voltdb.compiler.VoltCompiler.VoltCompilerException;
import org.voltdb.types.UDFType;

public class UDFCompiler {

    public static boolean processCreateLibraryStatement(Matcher statementMatcher, Database db) {
        String libraryName = statementMatcher.group("libraryName");
        String filePath = statementMatcher.group("filePath");
        filePath = filePath.substring(1, filePath.length()-1); // Remove the quotation marks.
        UDFLibrary udflib = db.getUdflibraries().add(libraryName);
        udflib.setLibraryname(libraryName);
        udflib.setFilepath(filePath);
        return true;
    }

    public static native int[] getFunctionPrototype(String libFilePath, String entryName);

    public static boolean processCreateFunctionStatement(
                          Matcher statementMatcher, Database db, VoltCompiler compiler
                          ) throws VoltCompilerException {
        String functionName = statementMatcher.group("functionName");
        String functionType = statementMatcher.group("functionType");
        StringBuilder msg = new StringBuilder(String.format("Cannot create function %s: ", functionName));

        // Currently we do not support aggregate UDF.
        if (functionType.equalsIgnoreCase("aggregate")) {
            msg.append("user-defined aggregate function is not supported yet.");
            throw compiler.new VoltCompilerException(msg.toString());
        }

        // Find the library this UDF belongs to.
        String libraryName = statementMatcher.group("libraryName");
        UDFLibrary udflib = db.getUdflibraries().get(libraryName);
        if (udflib == null) {
            msg.append(String.format("library %s was not found.", libraryName));
            throw compiler.new VoltCompilerException(msg.toString());
        }

        String entryName = statementMatcher.group("entryName");
        UDF udf = udflib.getLoadedudfs().add(functionName);
        udf.setFunctionname(functionName);
        udf.setFunctiontype(UDFType.SCALAR.getValue());
        udf.setSourcelibrary(udflib);
        udf.setEntryname(entryName);
        // Get the function prototype
        int[] prototype = getFunctionPrototype(udflib.getFilepath(), entryName);
        udf.setReturntype(prototype[0]);
        CatalogMap<UDFArgument> arguments = udf.getArguments();
        for (int i=1; i<prototype.length; i++) {
            int index = i-1;
            UDFArgument argument = arguments.add(String.valueOf(index));
            argument.setIndex(index);
            argument.setArgumenttype(prototype[i]);
        }
        return true;
    }
}