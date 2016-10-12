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

package org.voltcore.utils;

import java.io.IOException;
import java.nio.ByteBuffer;

/**
 * Interface for serializing an Object at a later time and place. At serialization
 * time a memory pool will be provided.
 *
 */
public interface DeferredSerialization {
    /**
     * Value to return for an empty message
     */
    public static final int EMPTY_MESSAGE_LENGTH = -1;

    /**
     * Serialize the Object contained in this DeferredSerialization.  If the
     * serialized Object fits into the passed buffer, put the result in that
     * buffer and return null.  If the serialized Object doesn't fit into the
     * passed buffer, allocate a new buffer of the right size and return it,
     * leaving the passed buffer unchanged.
     * @param buf Tries to serialize into this buffer.
     * @return A buffer with the serialized result if the serialized result doesn't fit
     * into the passed buffer.
     * @throws IOException Thrown here because FastSerialzier throws IOException
     */
    ByteBuffer serialize(ByteBuffer buf) throws IOException;

    /**
     * A deferred serialization might not be able to take place if a stream is closed
     * so a method for canceling the serialization and freeing associated resources must be provided.
     */
    void cancel();

    int getSerializedSize() throws IOException;
}
