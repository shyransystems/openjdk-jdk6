/*
 * $Id: FastInfosetDataContentHandler.java,v 1.1.1.1 2006/01/27 13:10:56 kumarjayanti Exp $
 * $Revision: 1.1.1.1 $
 * $Date: 2006/01/27 13:10:56 $
 */

/*
 * Copyright 2006 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Sun designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Sun in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 */
package com.sun.xml.internal.messaging.saaj.soap;

import java.awt.datatransfer.DataFlavor;
import java.io.IOException;
import java.io.OutputStream;
import java.io.InputStream;

import javax.activation.*;
import javax.xml.transform.Source;

import com.sun.xml.internal.messaging.saaj.util.FastInfosetReflection;

/**
 * JAF data handler for Fast Infoset content
 *
 * @author Santiago Pericas-Geertsen
 */
public class FastInfosetDataContentHandler implements DataContentHandler {
    public final String STR_SRC = "com.sun.xml.internal.org.jvnet.fastinfoset.FastInfosetSource";

    public FastInfosetDataContentHandler() {
    }

    /**
     * return the DataFlavors for this <code>DataContentHandler</code>
     * @return The DataFlavors.
     */
    public DataFlavor[] getTransferDataFlavors() { // throws Exception;
        DataFlavor flavors[] = new DataFlavor[1];
        flavors[0] = new ActivationDataFlavor(
                FastInfosetReflection.getFastInfosetSource_class(),
                "application/fastinfoset", "Fast Infoset");
        return flavors;
    }

    /**
     * return the Transfer Data of type DataFlavor from InputStream
     * @param df The DataFlavor.
     * @param ins The InputStream corresponding to the data.
     * @return The constructed Object.
     */
    public Object getTransferData(DataFlavor flavor, DataSource dataSource)
        throws IOException
    {
        if (flavor.getMimeType().startsWith("application/fastinfoset")) {
            try {
                if (flavor.getRepresentationClass().getName().equals(STR_SRC)) {
                    return FastInfosetReflection.FastInfosetSource_new(
                        dataSource.getInputStream());
                }
            }
            catch (Exception e) {
                throw new IOException(e.getMessage());
            }
        }
        return null;
    }

    public Object getContent(DataSource dataSource) throws IOException {
        try {
            return FastInfosetReflection.FastInfosetSource_new(
                dataSource.getInputStream());
        }
        catch (Exception e) {
            throw new IOException(e.getMessage());
        }
    }

    /**
     * construct an object from a byte stream
     * (similar semantically to previous method, we are deciding
     *  which one to support)
     */
    public void writeTo(Object obj, String mimeType, OutputStream os)
        throws IOException
    {
        if (!mimeType.equals("application/fastinfoset")) {
            throw new IOException("Invalid content type \"" + mimeType
                + "\" for FastInfosetDCH");
        }

        try {
            InputStream is = FastInfosetReflection.FastInfosetSource_getInputStream(
                (Source) obj);

            int n; byte[] buffer = new byte[4096];
            while ((n = is.read(buffer)) != -1) {
                os.write(buffer, 0, n);
            }
        }
        catch (Exception ex) {
            throw new IOException(
                "Error copying FI source to output stream " + ex.getMessage());
        }
    }
}
